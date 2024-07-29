#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ixgnpreader.h"
#include <stdio.h>
#include <string.h>

#define GST_CAT_DEFAULT gst_np_reader_debug
#define _IXG_DEFAULT_SHOW_CPU_LOAD FALSE
#define _IXG_DEFAULT_BITRATE_INTERVAL 1000
#define _IXG_DEFAULT_SHOW_BITRATE TRUE

/* pad templates */
static GstStaticPadTemplate gst_np_reader_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_np_reader_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC(gst_np_reader_debug);

enum { // ixg attribute
  _IXG_ATTR_0,
  _IXG_ATTR_SHOW_CPU_LOAD,
  _IXG_ATTR_BITRATE_INTERVAL,
  _IXG_ATTR_SHOW_BITRATE
};

/* GstNPReader signals and args */
enum { SIGNAL_ON_BITRATE, LAST_SIGNAL };

struct _GstNPReader {
  GstBaseTransform parent;

  GstPad *sinkpad;
  GstPad *srcpad;
  GError *error;

  GstClockTime prev_timestamp;
  gdouble _ixg_fps;
  guint32 _ixg_frame_count;
  guint64 _ixg_frame_count_total;

  gdouble _ixg_bps;
  gdouble _ixg_mean_bps;
  gdouble *bps_window_buffer;
  guint32 bps_window_size;
  guint64 _ixg_byte_count;
  guint64 _ixg_byte_count_total;
  guint _ixg_bps_interval;
  guint _ixg_bps_running_interval;
  GMutex _ixg_byte_count_mutex;
  GMutex _ixg_bps_mutex;
  GMutex _ixg_mean_bps_mutex;
  guint _ixg_bps_source_id;

  guint32 prev_cpu_total;
  guint32 prev_cpu_idle;

  gchar *show_location;

  /* Properties */
  gboolean show_cpu_load;
  gboolean _ixg_show_bitrate_interval;
};

struct _GstNPReaderClass {
  GstBaseTransformClass parent_class;
};

/* class initialization */
#define gst_np_reader_parent_class parent_class
G_DEFINE_TYPE(GstNPReader, gst_np_reader, GST_TYPE_BASE_TRANSFORM);

static guint gst_np_reader_signals[LAST_SIGNAL] = {0};

static void gst_np_reader_class_init(GstNPReaderClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

  gobject_class->set_property = gst_np_reader_set_property;
  gobject_class->get_property = gst_np_reader_get_property;

  g_object_class_install_property(
      gobject_class, _IXG_ATTR_SHOW_CPU_LOAD,
      g_param_spec_boolean("show-cpu-load", "show CPU load",
                           "show the CPU load info.",
                           _IXG_DEFAULT_SHOW_CPU_LOAD, G_PARAM_WRITABLE));

  g_object_class_install_property(
      gobject_class, _IXG_ATTR_BITRATE_INTERVAL,
      g_param_spec_uint(
          "bitrate-interval", "Interval between bitrate calculation in ms",
          "Interval between two calculations in ms, this will run even when no "
          "buffers are received",
          0, G_MAXINT, _IXG_DEFAULT_BITRATE_INTERVAL, G_PARAM_WRITABLE));

  g_object_class_install_property(
      gobject_class, _IXG_ATTR_SHOW_BITRATE,
      g_param_spec_boolean("show-bitrate", "Show Bitrate", "Show the Bitrate.",
                           _IXG_DEFAULT_SHOW_BITRATE, G_PARAM_WRITABLE));

  gst_np_reader_signals[SIGNAL_ON_BITRATE] =
      g_signal_new("on-bitrate", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                   NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);

  base_transform_class->start = GST_DEBUG_FUNCPTR(gst_np_reader_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_np_reader_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR(gst_np_reader_transform_ip);

  gst_element_class_set_static_metadata(
      element_class, "Show performance element", "Generic",
      "Show pipeline performance",
      "Nityanand Prasad <nityanand.prasad@tesseractesports.com>");

  gst_element_class_add_pad_template(
      element_class, gst_static_pad_template_get(&gst_np_reader_src_template));
  gst_element_class_add_pad_template(
      element_class, gst_static_pad_template_get(&gst_np_reader_sink_template));
}

static void gst_np_reader_init(GstNPReader *reader) {
  gst_np_reader_clear(reader);

  reader->show_cpu_load = _IXG_DEFAULT_SHOW_CPU_LOAD;
  reader->_ixg_bps_interval = _IXG_DEFAULT_BITRATE_INTERVAL;
  reader->_ixg_bps_running_interval = _IXG_DEFAULT_BITRATE_INTERVAL;

  reader->_ixg_show_bitrate_interval = _IXG_DEFAULT_SHOW_BITRATE;

  g_mutex_init(&reader->_ixg_byte_count_mutex);
  g_mutex_init(&reader->_ixg_bps_mutex);
  g_mutex_init(&reader->_ixg_mean_bps_mutex);

  gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM_CAST(reader), TRUE);
  gst_base_transform_set_passthrough(GST_BASE_TRANSFORM_CAST(reader), TRUE);
}

void gst_np_reader_set_property(GObject *object, guint property_id,
                                const GValue *value, GParamSpec *pspec) {
  GstNPReader *reader = GST_NP_READER(object);

  switch (property_id) {
  case _IXG_ATTR_SHOW_CPU_LOAD:
    GST_OBJECT_LOCK(reader);
    reader->show_cpu_load = g_value_get_boolean(value);
    GST_OBJECT_UNLOCK(reader);
    break;
  case _IXG_ATTR_BITRATE_INTERVAL:
    GST_OBJECT_LOCK(reader);
    reader->_ixg_bps_interval = g_value_get_uint(value);
    GST_OBJECT_UNLOCK(reader);
    break;
  case _IXG_ATTR_SHOW_BITRATE:
    GST_OBJECT_LOCK(reader);
    reader->_ixg_show_bitrate_interval = g_value_get_boolean(value);
    GST_OBJECT_UNLOCK(reader);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

void gst_np_reader_get_property(GObject *object, guint property_id,
                                GValue *value, GParamSpec *pspec) {
  GstNPReader *reader = GST_NP_READER(object);

  switch (property_id) {
  case _IXG_ATTR_SHOW_CPU_LOAD:
    GST_OBJECT_LOCK(reader);
    g_value_set_boolean(value, reader->show_cpu_load);
    GST_OBJECT_UNLOCK(reader);
    break;
  case _IXG_ATTR_BITRATE_INTERVAL:
    GST_OBJECT_LOCK(reader);
    g_value_set_uint(value, reader->_ixg_bps_interval);
    GST_OBJECT_UNLOCK(reader);
    break;
  case _IXG_ATTR_SHOW_BITRATE:
    GST_OBJECT_LOCK(reader);
    g_value_set_boolean(value, reader->_ixg_show_bitrate_interval);
    GST_OBJECT_UNLOCK(reader);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static gboolean gst_np_reader_update_bps(void *data) {
  guint buffer_current_idx;
  GstNPReader *reader;
  guint byte_count;
  gdouble _bps, _mean_bps;

  g_return_val_if_fail(data, FALSE);

  reader = GST_NP_READER(data);

  g_mutex_lock(&reader->_ixg_byte_count_mutex);
  byte_count = reader->_ixg_byte_count;
  reader->_ixg_byte_count = G_GUINT64_CONSTANT(0);
  g_mutex_unlock(&reader->_ixg_byte_count_mutex);

  g_mutex_lock(&reader->_ixg_mean_bps_mutex);
  _mean_bps = reader->_ixg_mean_bps;
  g_mutex_unlock(&reader->_ixg_mean_bps_mutex);

  /* Calculate bits per second */
  _bps = byte_count * GST_NP_READER_BITS_PER_BYTE /
         (reader->_ixg_bps_running_interval / GST_NP_READER_MS_PER_S);

  /* Update _ixg_bps average */
  if (!reader->bps_window_size) {
    _mean_bps = gst_np_reader_update_average(reader->_ixg_byte_count_total,
                                             _bps, _mean_bps);
  } else {
    /*
     * Moving average uses a circular buffer, get index for next value which
     * is the oldest sample, this is the same as the value where the new sample
     * is to be stored
     */
    buffer_current_idx =
        (reader->_ixg_byte_count_total) % reader->bps_window_size;

    _mean_bps = gst_np_reader_update_moving_average(
        reader->bps_window_size, _mean_bps, _bps,
        reader->bps_window_buffer[buffer_current_idx]);

    reader->bps_window_buffer[buffer_current_idx] = _bps;
  }
  g_mutex_lock(&reader->_ixg_mean_bps_mutex);
  reader->_ixg_mean_bps = _mean_bps;
  g_mutex_unlock(&reader->_ixg_mean_bps_mutex);

  g_mutex_lock(&reader->_ixg_bps_mutex);
  reader->_ixg_bps = _bps;
  g_mutex_unlock(&reader->_ixg_bps_mutex);

  reader->_ixg_byte_count_total++;

  g_signal_emit_by_name(reader, "on-bitrate", _mean_bps);

  return TRUE;
}

static gboolean gst_np_reader_start(GstBaseTransform *trans) {
  GstNPReader *reader = GST_NP_READER(trans);

  // Clear any existing performance data
  gst_np_reader_clear(reader);

  // If window size is defined, allocate memory for the buffer
  if (reader->bps_window_size) {
    reader->bps_window_buffer =
        g_malloc0((reader->bps_window_size) * sizeof(gdouble));

    if (!reader->bps_window_buffer) {
      GST_ERROR_OBJECT(reader, "Unable to allocate memory");
      return FALSE;
    }
  }

  reader->_ixg_bps_running_interval = reader->_ixg_bps_interval;

  // Set up a timeout callback to update bitrate
  reader->_ixg_bps_source_id = g_timeout_add(reader->_ixg_bps_interval,
                                             gst_np_reader_update_bps, reader);

  reader->error = g_error_new(GST_CORE_ERROR, GST_CORE_ERROR_TAG,
                              "Performance Information");
  return TRUE;
}

static gboolean gst_np_reader_stop(GstBaseTransform *trans) {
  GstNPReader *reader = GST_NP_READER(trans);

  gst_np_reader_clear(reader);

  // g_free(perf->bps_window_buffer);

  g_source_remove(reader->_ixg_bps_source_id);

  if (reader->error)
    g_error_free(reader->error);

  return TRUE;
  return TRUE;
}
static guint32 gst_np_reader_compute_cpu(GstNPReader *self,
                                         guint32 current_idle,
                                         guint32 current_total) {
  guint32 busy = 0;
  guint32 idle = 0;
  guint32 total = 0;

  g_return_val_if_fail(self, -1);

  /* Calculate the CPU usage since last time we checked */
  idle = current_idle - self->prev_cpu_idle;
  total = current_total - self->prev_cpu_total;

  /* Update the total and idle CPU for the next check */
  self->prev_cpu_total = current_total;
  self->prev_cpu_idle = current_idle;

  /* Avoid a division by zero */
  if (0 == total) {
    return 0;
  }

  /* - CPU usage is the fraction of time the processor spent busy:
   * [0.0, 1.0].
   *
   * - We want to express this as a percentage [0% - 100%].
   *
   * - We want to avoid, when possible, using floating
   * point operations (some SoC still don't have a FP unit).
   *
   * - Scaling to 1000 allows us to round (nearest integer) by summing
   * 5 and then scaling down back to 100 by dividing by
   * 10. Otherwise, we would've lost the decimals due to integer
   * truncation.
   */
  busy = total - idle;
  return (1000 * busy / total + 5) / 10;
}

static GstFlowReturn gst_np_reader_transform_ip(GstBaseTransform *trans,
                                                GstBuffer *buf) {
  GstNPReader *reader = GST_NP_READER(trans);
  GstClockTime time = gst_util_get_timestamp();
  GstClockTime diff = GST_CLOCK_DIFF(reader->prev_timestamp, time);

  if (!GST_CLOCK_TIME_IS_VALID(reader->prev_timestamp) ||
      (GST_CLOCK_TIME_IS_VALID(time) && diff >= GST_SECOND)) {
    gdouble time_factor, fps;
    guint idx;
    gchar info[GST_NP_READER_MSG_MAX_SIZE];
    gboolean show_cpu_load;
    gboolean show_bitrate_interval;
    gdouble bps, mean_bps;

    time_factor = 1.0 * diff / GST_SECOND;

    /* Calculate frames per second */
    fps = reader->_ixg_frame_count / time_factor;

    /* Update fps average */
    reader->_ixg_fps = gst_np_reader_update_average(
        reader->_ixg_frame_count_total, fps, reader->_ixg_fps);
    reader->_ixg_frame_count_total++;

    g_mutex_lock(&reader->_ixg_bps_mutex);
    bps = reader->_ixg_bps;
    g_mutex_unlock(&reader->_ixg_bps_mutex);

    g_mutex_lock(&reader->_ixg_mean_bps_mutex);
    mean_bps = reader->_ixg_mean_bps;
    g_mutex_unlock(&reader->_ixg_mean_bps_mutex);

    /* Create performance info string */
    show_bitrate_interval = reader->_ixg_show_bitrate_interval;
    idx =
        g_snprintf(info, GST_NP_READER_MSG_MAX_SIZE,
                   "ixg_reader: %s; timestamp: %" GST_TIME_FORMAT "; "
                   "bps: %0.03f; avg_bps: %0.03f; ",
                   GST_OBJECT_NAME(reader), GST_TIME_ARGS(time), bps, mean_bps);

    gst_np_reader_reset(reader);
    reader->prev_timestamp = time;

    GST_OBJECT_LOCK(reader);
    show_cpu_load = reader->show_cpu_load;
    GST_OBJECT_UNLOCK(reader);

    if (show_cpu_load) {
      guint32 cpu_load;
      gst_np_reader_cpu_get_load(reader, &cpu_load);
      idx = g_snprintf(&info[idx], GST_NP_READER_MSG_MAX_SIZE - idx,
                       "; cpu: %d; ", cpu_load);
    }

    gst_element_post_message((GstElement *)reader,
                             gst_message_new_info((GstObject *)reader,
                                                  reader->error,
                                                  (const gchar *)info));

    GST_INFO_OBJECT(reader, "%s", info);
  }

  reader->_ixg_frame_count++;
  g_mutex_lock(&reader->_ixg_byte_count_mutex);
  reader->_ixg_byte_count += gst_buffer_get_size(buf);
  g_mutex_unlock(&reader->_ixg_byte_count_mutex);

  return GST_FLOW_OK;
}
static void gst_np_reader_reset(GstNPReader *reader) {
  g_return_if_fail(reader);

  reader->_ixg_frame_count = 0;
}

#ifdef IS_LINUX
static gboolean gst_np_reader_cpu_get_load(GstNPReader *reader,
                                           guint32 *cpu_load) {
  gboolean cpu_load_found = FALSE;
  guint32 user, nice, sys, idle, iowait, irq, softirq, steal;
  guint32 total = 0;
  gchar name[4];
  FILE *fp;

  g_return_val_if_fail(reader, FALSE);
  g_return_val_if_fail(cpu_load, FALSE);

  /* Default value in case of failure */
  *cpu_load = -1;

  /* Read the overall system information */
  fp = fopen("/proc/stat", "r");

  if (fp == NULL) {
    GST_ERROR("/proc/stat not found");
    goto cpu_failed;
  }
  /* Scan the file line by line */
  while (fscanf(fp, "%4s %d %d %d %d %d %d %d %d", name, &user, &nice, &sys,
                &idle, &iowait, &irq, &softirq, &steal) != EOF) {
    if (strcmp(name, "cpu") == 0) {
      cpu_load_found = TRUE;
      break;
    }
  }

  fclose(fp);

  if (!cpu_load_found) {
    goto cpu_failed;
  }
  GST_DEBUG("CPU stats-> user: %d; nice: %d; sys: %d; idle: %d "
            "iowait: %d; irq: %d; softirq: %d; steal: %d",
            user, nice, sys, idle, iowait, irq, softirq, steal);

  /* Calculate the total CPU time */
  total = user + nice + sys + idle + iowait + irq + softirq + steal;

  *cpu_load = gst_np_reader_compute_cpu(reader, idle, total);

  return TRUE;

cpu_failed:
  GST_ERROR_OBJECT(reader, "Failed to get the CPU load");
  return FALSE;
}

#else /* Unknown OS */
static gboolean gst_np_reader_cpu_get_load(GstNPReader *reader,
                                           guint32 *cpu_load) {
  g_return_val_if_fail(reader, FALSE);
  g_return_val_if_fail(cpu_load, FALSE);

  *cpu_load = -1;

  /* Not really an error, we just don't know how to measure CPU on this OS */
  return TRUE;
}
#endif

static void gst_np_reader_clear(GstNPReader *reader) {
  g_return_if_fail(reader);

  gst_np_reader_reset(reader);

  reader->_ixg_fps = 0.0;
  reader->_ixg_frame_count_total = G_GUINT64_CONSTANT(0);

  reader->_ixg_mean_bps = 0.0;
  reader->_ixg_byte_count_total = G_GUINT64_CONSTANT(0);
  reader->_ixg_byte_count = G_GUINT64_CONSTANT(0);

  reader->prev_timestamp = GST_CLOCK_TIME_NONE;
  reader->prev_cpu_total = 0;
  reader->prev_cpu_idle = 0;
}

static gdouble gst_np_reader_update_average(guint64 count, gdouble current,
                                            gdouble old) {
  gdouble ret = 0;

  if (count != 0) {
    ret = ((count - 1) * old + current) / count;
  }

  return ret;
}

static gdouble gst_np_reader_update_moving_average(guint64 window_size,
                                                   gdouble old_average,
                                                   gdouble new_sample,
                                                   gdouble old_sample) {
  gdouble ret = 0;

  if (window_size != 0) {
    ret = (old_average * window_size - old_sample + new_sample) / window_size;
  }

  return ret;
}

static gboolean plugin_init(GstPlugin *plugin) {

  GST_DEBUG_CATEGORY_INIT(gst_np_reader_debug, "ixgnpreader", 0,
                          "Debug category for np reader element");

  return gst_element_register(plugin, "ixgnpreader", GST_RANK_NONE,
                              GST_TYPE_NP_READER);
}

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://www.nityanand.com"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, ixgnpreader,
                  "Get pipeline performance data", plugin_init, VERSION, "LGPL",
                  PACKAGE_NAME, GST_PACKAGE_ORIGIN)
