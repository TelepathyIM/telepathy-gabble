/**
 * SECTION:debug-ansi
 * @title: ANSI codes for debug messages
 * @short_description: used to colorize log messages for clarity
 *
 * This header provides some ANSI escapes you can use to make debug messages
 * more colourful. Please use sparingly.
 */

#ifndef __TP_DEBUG_ANSI_H__
#define __TP_DEBUG_ANSI_H__

/**
 * TP_ANSI_RESET:
 *
 * Reset all attributes.
 */
#define TP_ANSI_RESET      "\x1b[0m"
/**
 * TP_ANSI_BOLD_ON:
 *
 * Set the bold attribute.
 */
#define TP_ANSI_BOLD_ON    "\x1b[1m"
/**
 * TP_ANSI_INVERSE_ON:
 *
 * Set the inverse video attribute.
 */
#define TP_ANSI_INVERSE_ON "\x1b[7m"
/**
 * TP_ANSI_BOLD_OFF:
 *
 * Clear the bold attribute.
 */
#define TP_ANSI_BOLD_OFF   "\x1b[22m"
/**
 * TP_ANSI_FG_BLACK:
 *
 * <!---->
 */
#define TP_ANSI_FG_BLACK   "\x1b[30m"
/**
 * TP_ANSI_FG_RED:
 *
 * <!---->
 */
#define TP_ANSI_FG_RED     "\x1b[31m"
/**
 * TP_ANSI_FG_GREEN:
 *
 * <!---->
 */
#define TP_ANSI_FG_GREEN   "\x1b[32m"
/**
 * TP_ANSI_FG_YELLOW:
 *
 * <!---->
 */
#define TP_ANSI_FG_YELLOW  "\x1b[33m"
/**
 * TP_ANSI_FG_BLUE:
 *
 * <!---->
 */
#define TP_ANSI_FG_BLUE    "\x1b[34m"
/**
 * TP_ANSI_FG_MAGENTA:
 *
 * <!---->
 */
#define TP_ANSI_FG_MAGENTA "\x1b[35m"
/**
 * TP_ANSI_FG_CYAN:
 *
 * <!---->
 */
#define TP_ANSI_FG_CYAN    "\x1b[36m"
/**
 * TP_ANSI_FG_WHITE:
 *
 * <!---->
 */
#define TP_ANSI_FG_WHITE   "\x1b[37m"
/**
 * TP_ANSI_BG_RED:
 *
 * <!---->
 */
#define TP_ANSI_BG_RED     "\x1b[41m"
/**
 * TP_ANSI_BG_GREEN:
 *
 * <!---->
 */
#define TP_ANSI_BG_GREEN   "\x1b[42m"
/**
 * TP_ANSI_BG_YELLOW:
 *
 * <!---->
 */
#define TP_ANSI_BG_YELLOW  "\x1b[43m"
/**
 * TP_ANSI_BG_BLUE:
 *
 * <!---->
 */
#define TP_ANSI_BG_BLUE    "\x1b[44m"
/**
 * TP_ANSI_BG_MAGENTA:
 *
 * <!---->
 */
#define TP_ANSI_BG_MAGENTA "\x1b[45m"
/**
 * TP_ANSI_BG_CYAN:
 *
 * <!---->
 */
#define TP_ANSI_BG_CYAN    "\x1b[46m"
/**
 * TP_ANSI_BG_WHITE:
 *
 * <!---->
 */
#define TP_ANSI_BG_WHITE   "\x1b[47m"

#endif /* __TP_DEBUG_ANSI_H__ */
