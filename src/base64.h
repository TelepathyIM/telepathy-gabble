
#ifndef __BASE64_H__
#define __BASE64_H__

#include <glib.h>

gchar *base64_encode (const GString *str);
GString *base64_decode (const gchar *str);

#endif /* __BASE64_H__ */

