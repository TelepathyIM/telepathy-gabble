#include <stdio.h>
#include <string.h>

#include <base64.h>

struct test {
  gchar *str;
  size_t len;
  gchar *encoded;
};

struct test tests[] = {
  { "c", 0,   "Yw==" },
  { "ca", 0,  "Y2E=" },
  { "car", 0, "Y2Fy" },
  { "carnal pleasure.", 0, "Y2FybmFsIHBsZWFzdXJlLg==" },
  { "carnal pleasure", 0,  "Y2FybmFsIHBsZWFzdXJl" },
  { "carnal pleasur", 0,   "Y2FybmFsIHBsZWFzdXI=" },
  { "carnal pleasu", 0,    "Y2FybmFsIHBsZWFzdQ==" },
  /* test long (> 76 / 4 * 3) string */
  {
    "1234567890"
    "1234567890"
    "1234567890"
    "1234567890"
    "1234567890"
    "1234567890",
    0,
    "MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM"
    "0NTY3\nODkw" },
  /* regression test: formerly we assumed that there was a NUL immediately
   * after the data */
  { "hello", 5, "aGVsbG8=" },
  { "hello\xff\xff", 5, "aGVsbG8=" },

  { NULL, 0, NULL }
};

int
main (void)
{
  gchar *s;
  GString *tmp1, *tmp2;
  struct test *t;

  for (t = tests; t->str != NULL; t++)
    {
      if (t->len == 0)
        t->len = strlen (t->str);
      s = base64_encode (t->len, t->str);
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
      tmp2 = g_string_new_len (t->str, t->len);
      g_assert (g_string_equal (tmp1, tmp2));
      g_string_free (tmp1, TRUE);
      g_string_free (tmp2, TRUE);
    }

  return 0;
}

