
#include <string.h>

#include <base64.h>

struct test {
  gchar *str;
  gchar *encoded;
};

struct test tests[] = {
  { "c",   "Yw==" },
  { "ca",  "Y2E=" },
  { "car", "Y2Fy" },
  { "carnal pleasure.", "Y2FybmFsIHBsZWFzdXJlLg==" },
  { "carnal pleasure",  "Y2FybmFsIHBsZWFzdXJl" },
  { "carnal pleasur",   "Y2FybmFsIHBsZWFzdXI=" },
  { "carnal pleasu",    "Y2FybmFsIHBsZWFzdQ==" },
  /* test long (> 76 / 4 * 3) string */
  { "1234567890"
    "1234567890"
    "1234567890"
    "1234567890"
    "1234567890"
    "1234567890",
    "MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM"
    "0NTY3\nODkw" },
  { NULL, NULL }
};

int
main (void)
{
  gchar *s;
  GString *tmp1, *tmp2;
  struct test *t;

  for (t = tests; t->str != NULL; t++)
    {
      s = base64_encode (strlen (t->str), t->str);
      g_assert (0 == strcmp (s, t->encoded));
      g_free (s);
    }

  /* test string with valid characters but invalid length */
  tmp1 = base64_decode ("AAA");
  g_assert (tmp1 == NULL);

  /* test string with valid length but invalid characters */
  tmp1 = base64_decode ("????");
  g_assert (tmp1 == NULL);

  /* test string with embedded newline */
  tmp1 = base64_decode ("bWF6\ndWxlbQ==");
  tmp2 = g_string_new ("mazulem");
  g_assert (tmp1);
  g_assert (g_string_equal (tmp1, tmp2));
  g_string_free (tmp1, TRUE);
  g_string_free (tmp2, TRUE);

  /* test string with misc whitespace */
  tmp1 = base64_decode ("bW F\r6\r\ndW\nxlbQ==\r\n");
  tmp2 = g_string_new ("mazulem");
  g_assert (tmp1);
  g_assert (g_string_equal (tmp1, tmp2));
  g_string_free (tmp1, TRUE);
  g_string_free (tmp2, TRUE);

  /* test string with embedded NULL */
  tmp1 = base64_decode ("Zm9vAGJhcg==");
  tmp2 = g_string_new_len ("foo\0bar", 7);
  g_assert (g_string_equal (tmp1, tmp2));
  g_string_free (tmp1, TRUE);
  g_string_free (tmp2, TRUE);

  for (t = tests; t->str != NULL; t++)
    {
      tmp1 = base64_decode (t->encoded);
      g_assert (tmp1);
      tmp2 = g_string_new (t->str);
      g_assert (g_string_equal (tmp1, tmp2));
      g_string_free (tmp1, TRUE);
      g_string_free (tmp2, TRUE);
    }

  return 0;
}

