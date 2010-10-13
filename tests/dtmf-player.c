#include "config.h"
#include "src/dtmf.h"

#include <telepathy-glib/util.h>

static void
test_to_char (void)
{
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_0), ==, '0');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_1), ==, '1');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_2), ==, '2');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_3), ==, '3');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_4), ==, '4');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_5), ==, '5');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_6), ==, '6');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_7), ==, '7');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_8), ==, '8');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_DIGIT_9), ==, '9');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_LETTER_A), ==,
      'A');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_LETTER_B), ==,
      'B');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_LETTER_C), ==,
      'C');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_LETTER_D), ==,
      'D');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_HASH), ==, '#');
  g_assert_cmpint (gabble_dtmf_event_to_char (TP_DTMF_EVENT_ASTERISK), ==,
      '*');
}

typedef struct {
    GabbleDTMFPlayer *dtmf_player;
    GString *log;
    GError *error /* initially NULL */ ;
} Fixture;

static void
fixture_log (Fixture *f,
    const gchar *format,
    ...)
{
  va_list ap;

  va_start (ap, format);
  g_string_append_vprintf (f->log, format, ap);
  g_string_append_c (f->log, '\n');
  va_end (ap);
}

static void
fixture_assert_log (Fixture *f,
    const gchar *expected)
{
  g_assert_cmpstr (f->log->str, ==, expected);
}

static void
started_tone_cb (GabbleDTMFPlayer *dtmf_player G_GNUC_UNUSED,
    guint event,
    Fixture *f)
{
  fixture_log (f, "started '%c'", gabble_dtmf_event_to_char (event));
}

static void
stopped_tone_cb (GabbleDTMFPlayer *dtmf_player G_GNUC_UNUSED,
    Fixture *f)
{
  fixture_log (f, "stopped");
}

static void
finished_cb (GabbleDTMFPlayer *dtmf_player G_GNUC_UNUSED,
    gboolean cancelled,
    Fixture *f)
{
  if (cancelled)
    fixture_log (f, "cancelled");
  else
    fixture_log (f, "finished");
}

static void
setup (Fixture *f,
    gconstpointer nil G_GNUC_UNUSED)
{
  g_type_init ();

  f->dtmf_player = gabble_dtmf_player_new ();
  g_assert (f->dtmf_player != NULL);

  f->log = g_string_new ("");

  g_signal_connect (f->dtmf_player, "started-tone",
      G_CALLBACK (started_tone_cb), f);
  g_signal_connect (f->dtmf_player, "stopped-tone",
      G_CALLBACK (stopped_tone_cb), f);
  g_signal_connect (f->dtmf_player, "finished",
      G_CALLBACK (finished_cb), f);
}

static void
teardown (Fixture *f,
    gconstpointer nil G_GNUC_UNUSED)
{
  g_signal_handlers_disconnect_matched (f->dtmf_player, G_SIGNAL_MATCH_DATA,
      0, 0, 0, 0, f);

  tp_clear_object (&f->dtmf_player);
  g_string_free (f->log, TRUE);
  g_clear_error (&f->error);
}

static void
test_noop (Fixture *f G_GNUC_UNUSED,
    gconstpointer nil G_GNUC_UNUSED)
{
}

static void
test_empty (Fixture *f,
    gconstpointer nil G_GNUC_UNUSED)
{
  gboolean ok;

  ok = gabble_dtmf_player_play (f->dtmf_player, "", 1, 1, 1, &f->error);
  g_assert_no_error (f->error);
  g_assert (ok);
  g_assert (!gabble_dtmf_player_is_active (f->dtmf_player));

  fixture_assert_log (f, "finished\n");
}

static void
test_cancel (Fixture *f,
    gconstpointer nil G_GNUC_UNUSED)
{
  gboolean ok;

  ok = gabble_dtmf_player_play (f->dtmf_player, "#", 10000, 1, 1, &f->error);
  g_assert_no_error (f->error);
  g_assert (ok);
  g_assert (gabble_dtmf_player_is_active (f->dtmf_player));

  gabble_dtmf_player_cancel (f->dtmf_player);
  g_assert (!gabble_dtmf_player_is_active (f->dtmf_player));

  fixture_assert_log (f,
      "started '#'\n"
      "stopped\n"
      "cancelled\n");
}

static void
test_cancel_in_gap (Fixture *f,
    gconstpointer nil G_GNUC_UNUSED)
{
  gboolean ok;

  ok = gabble_dtmf_player_play (f->dtmf_player, "#*", 1, 10000, 1, &f->error);
  g_assert_no_error (f->error);
  g_assert (ok);
  g_assert (gabble_dtmf_player_is_active (f->dtmf_player));
  fixture_assert_log (f, "started '#'\n");

  gabble_dtmf_player_cancel (f->dtmf_player);
  g_assert (!gabble_dtmf_player_is_active (f->dtmf_player));

  fixture_assert_log (f,
      "started '#'\n"
      "stopped\n"
      "cancelled\n");
}

static void
test_sequence (Fixture *f,
    gconstpointer nil G_GNUC_UNUSED)
{
  gboolean ok;

  ok = gabble_dtmf_player_play (f->dtmf_player, "*12,3#", 1, 1, 1,
      &f->error);
  g_assert_no_error (f->error);
  g_assert (ok);
  g_assert (gabble_dtmf_player_is_active (f->dtmf_player));
  fixture_assert_log (f, "started '*'\n");

  while (gabble_dtmf_player_is_active (f->dtmf_player))
    g_main_context_iteration (NULL, TRUE);

  fixture_assert_log (f,
      "started '*'\n"
      "stopped\n"
      "started '1'\n"
      "stopped\n"
      "started '2'\n"
      "stopped\n"
      /* at this point we'd wait longer than usual - you can't tell here,
       * because we have 1ms as the gap time and also as the pause time,
       * and we're not keeping track anyway */
      "started '3'\n"
      "stopped\n"
      "started '#'\n"
      "stopped\n"
      "finished\n");
}

static gboolean
timeout_cb (gpointer nil G_GNUC_UNUSED)
{
  g_error ("timed out");
  g_assert_not_reached ();
}

int
main (int argc,
    char **argv)
{
#define TEST_PREFIX "/dtmf-player/"
#define FIXTURE_TEST(x) \
  g_test_add (TEST_PREFIX #x, Fixture, NULL, setup, test_ ## x, teardown)

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_timeout_add_seconds (10, timeout_cb, NULL);

  g_test_add_func (TEST_PREFIX "to_char", test_to_char);

  FIXTURE_TEST (noop);
  FIXTURE_TEST (empty);
  FIXTURE_TEST (cancel);
  FIXTURE_TEST (cancel_in_gap);
  FIXTURE_TEST (sequence);

  return g_test_run ();
}
