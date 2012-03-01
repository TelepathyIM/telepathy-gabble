/*
 * jingle-types.h - Header for Jingle-related enums and typedefs
 * Copyright © 2008–2012 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef GABBLE_JINGLE_ENUMS_H
#define GABBLE_JINGLE_ENUMS_H

typedef struct _GabbleJingleFactory GabbleJingleFactory;
typedef struct _GabbleJingleSession GabbleJingleSession;
typedef struct _GabbleJingleContent GabbleJingleContent;
typedef struct _GabbleJingleTransportGoogle GabbleJingleTransportGoogle;
typedef struct _GabbleJingleTransportRawUdp GabbleJingleTransportRawUdp;
typedef struct _GabbleJingleTransportIceUdp GabbleJingleTransportIceUdp;
typedef struct _GabbleJingleMediaRtp GabbleJingleMediaRtp;
typedef struct _GabbleJingleShare GabbleJingleShare;
typedef struct _JingleCandidate JingleCandidate;

typedef enum { /*< skip >*/
  /* not a jingle message */
  JINGLE_DIALECT_ERROR,
  /* old libjingle3 gtalk variant */
  JINGLE_DIALECT_GTALK3,
  /* new gtalk variant */
  JINGLE_DIALECT_GTALK4,
  /* jingle in the old 0.15 version days */
  JINGLE_DIALECT_V015,
  /* current jingle standard */
  JINGLE_DIALECT_V032
} JingleDialect;

#define JINGLE_IS_GOOGLE_DIALECT(d)\
    ((d == JINGLE_DIALECT_GTALK3) || (d == JINGLE_DIALECT_GTALK4))

typedef enum { /*< skip >*/
  JINGLE_STATE_INVALID = -1,
  JINGLE_STATE_PENDING_CREATED = 0,
  JINGLE_STATE_PENDING_INITIATE_SENT,
  JINGLE_STATE_PENDING_INITIATED,
  JINGLE_STATE_PENDING_ACCEPT_SENT,
  JINGLE_STATE_ACTIVE,
  JINGLE_STATE_ENDED,
  MAX_JINGLE_STATES
} JingleState;

typedef enum { /*< skip >*/
  JINGLE_ACTION_UNKNOWN,
  JINGLE_ACTION_CONTENT_ACCEPT,
  JINGLE_ACTION_CONTENT_ADD,
  JINGLE_ACTION_CONTENT_MODIFY,
  JINGLE_ACTION_CONTENT_REMOVE,
  JINGLE_ACTION_CONTENT_REPLACE,
  JINGLE_ACTION_CONTENT_REJECT,
  JINGLE_ACTION_SESSION_ACCEPT,
  JINGLE_ACTION_SESSION_INFO,
  JINGLE_ACTION_SESSION_INITIATE,
  JINGLE_ACTION_SESSION_TERMINATE,
  JINGLE_ACTION_TRANSPORT_INFO,
  JINGLE_ACTION_TRANSPORT_ACCEPT,
  JINGLE_ACTION_DESCRIPTION_INFO,
  JINGLE_ACTION_INFO
} JingleAction;

typedef enum { /*< skip >*/
  JINGLE_CONTENT_SENDERS_NONE,
  JINGLE_CONTENT_SENDERS_INITIATOR,
  JINGLE_CONTENT_SENDERS_RESPONDER,
  JINGLE_CONTENT_SENDERS_BOTH
} JingleContentSenders;

typedef enum { /*< skip >*/
  JINGLE_TRANSPORT_UNKNOWN,
  JINGLE_TRANSPORT_GOOGLE_P2P,
  JINGLE_TRANSPORT_RAW_UDP,
  JINGLE_TRANSPORT_ICE_UDP,
} JingleTransportType;

typedef enum { /*< skip >*/
  JINGLE_TRANSPORT_PROTOCOL_UDP,
  JINGLE_TRANSPORT_PROTOCOL_TCP
} JingleTransportProtocol;

typedef enum { /*< skip >*/
  JINGLE_CANDIDATE_TYPE_LOCAL,
  JINGLE_CANDIDATE_TYPE_STUN,
  JINGLE_CANDIDATE_TYPE_RELAY
} JingleCandidateType;

typedef enum
{
  JINGLE_REASON_UNKNOWN,
  JINGLE_REASON_ALTERNATIVE_SESSION,
  JINGLE_REASON_BUSY,
  JINGLE_REASON_CANCEL,
  JINGLE_REASON_CONNECTIVITY_ERROR,
  JINGLE_REASON_DECLINE,
  JINGLE_REASON_EXPIRED,
  JINGLE_REASON_FAILED_APPLICATION,
  JINGLE_REASON_FAILED_TRANSPORT,
  JINGLE_REASON_GENERAL_ERROR,
  JINGLE_REASON_GONE,
  JINGLE_REASON_INCOMPATIBLE_PARAMETERS,
  JINGLE_REASON_MEDIA_ERROR,
  JINGLE_REASON_SECURITY_ERROR,
  JINGLE_REASON_SUCCESS,
  JINGLE_REASON_TIMEOUT,
  JINGLE_REASON_UNSUPPORTED_APPLICATIONS,
  JINGLE_REASON_UNSUPPORTED_TRANSPORTS
} JingleReason;


#endif /* GABBLE_JINGLE_ENUMS_H */
