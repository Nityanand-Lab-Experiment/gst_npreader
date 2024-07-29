#ifndef _IXGNPREADER_H_
#define _IXGNPREADER_H_

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

/* The message is variable length depending on configuration */
#define GST_NP_READER_MSG_MAX_SIZE 4096

#define GST_NP_READER_BITS_PER_BYTE 8

#define GST_NP_READER_MS_PER_S 1000.0

G_BEGIN_DECLS
typedef struct _GstNPReader GstNPReader;
typedef struct _GstNPReaderClass GstNPReaderClass;

#define GST_TYPE_NP_READER (gst_np_reader_get_type())
#define GST_NP_READER(obj)                                                     \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NP_READER, GstNPReader))
#define GST_NP_READER_CLASS(klass)                                             \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NP_READER, GstNPReaderClass))
#define GST_IS_NP_READER(obj)                                                  \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NP_READER))
#define GST_IS_NP_READER_CLASS(obj)                                            \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NP_READER))

GType gst_np_reader_get_type(void);

G_END_DECLS

/* prototypes */
static void gst_np_reader_set_property(GObject *object, guint property_id,
                                       const GValue *value, GParamSpec *pspec);
static void gst_np_reader_get_property(GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec);

static GstFlowReturn gst_np_reader_transform_ip(GstBaseTransform *trans,
                                                GstBuffer *buf);
static gboolean gst_np_reader_start(GstBaseTransform *trans);
static gboolean gst_np_reader_stop(GstBaseTransform *trans);

static void gst_np_reader_reset(GstNPReader *reader);
static void gst_np_reader_clear(GstNPReader *reader);
static gdouble gst_np_reader_update_average(guint64 count, gdouble current,
                                            gdouble old);
static double gst_np_reader_update_moving_average(guint64 window_size,
                                                  gdouble old_average,
                                                  gdouble new_sample,
                                                  gdouble old_sample);
static gboolean gst_np_reader_update_bps(void *data);
static gboolean gst_np_reader_cpu_get_load(GstNPReader *reader,
                                           guint32 *cpu_load);
static guint32 gst_np_reader_compute_cpu(GstNPReader *reader, guint32 idle,
                                         guint32 total);

#endif
