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

typedef struct _WockyJingleFactory WockyJingleFactory;
typedef struct _WockyJingleSession WockyJingleSession;
typedef struct _WockyJingleContent WockyJingleContent;
typedef struct _WockyJingleTransportGoogle WockyJingleTransportGoogle;
typedef struct _WockyJingleTransportRawUdp WockyJingleTransportRawUdp;
typedef struct _WockyJingleTransportIceUdp WockyJingleTransportIceUdp;
typedef struct _WockyJingleMediaRtp WockyJingleMediaRtp;
typedef struct _GabbleJingleShare GabbleJingleShare;
typedef struct _WockyJingleCandidate WockyJingleCandidate;

typedef enum { /*< skip >*/
  /* not a jingle message */
  WOCKY_JINGLE_DIALECT_ERROR,
  /* old libjingle3 gtalk variant */
  WOCKY_JINGLE_DIALECT_GTALK3,
  /* new gtalk variant */
  WOCKY_JINGLE_DIALECT_GTALK4,
  /* jingle in the old 0.15 version days */
  WOCKY_JINGLE_DIALECT_V015,
  /* current jingle standard */
  WOCKY_JINGLE_DIALECT_V032
} WockyJingleDialect;

#define WOCKY_JINGLE_DIALECT_IS_GOOGLE(d)\
    ((d == WOCKY_JINGLE_DIALECT_GTALK3) || (d == WOCKY_JINGLE_DIALECT_GTALK4))

typedef enum { /*< skip >*/
  WOCKY_JINGLE_STATE_INVALID = -1,
  WOCKY_JINGLE_STATE_PENDING_CREATED = 0,
  WOCKY_JINGLE_STATE_PENDING_INITIATE_SENT,
  WOCKY_JINGLE_STATE_PENDING_INITIATED,
  WOCKY_JINGLE_STATE_PENDING_ACCEPT_SENT,
  WOCKY_JINGLE_STATE_ACTIVE,
  WOCKY_JINGLE_STATE_ENDED,
  WOCKY_N_JINGLE_STATES
} WockyJingleState;

typedef enum { /*< skip >*/
  WOCKY_JINGLE_ACTION_UNKNOWN,
  WOCKY_JINGLE_ACTION_CONTENT_ACCEPT,
  WOCKY_JINGLE_ACTION_CONTENT_ADD,
  WOCKY_JINGLE_ACTION_CONTENT_MODIFY,
  WOCKY_JINGLE_ACTION_CONTENT_REMOVE,
  WOCKY_JINGLE_ACTION_CONTENT_REPLACE,
  WOCKY_JINGLE_ACTION_CONTENT_REJECT,
  WOCKY_JINGLE_ACTION_SESSION_ACCEPT,
  WOCKY_JINGLE_ACTION_SESSION_INFO,
  WOCKY_JINGLE_ACTION_SESSION_INITIATE,
  WOCKY_JINGLE_ACTION_SESSION_TERMINATE,
  WOCKY_JINGLE_ACTION_TRANSPORT_INFO,
  WOCKY_JINGLE_ACTION_TRANSPORT_ACCEPT,
  WOCKY_JINGLE_ACTION_DESCRIPTION_INFO,
  WOCKY_JINGLE_ACTION_INFO
} WockyJingleAction;

typedef enum { /*< skip >*/
  WOCKY_JINGLE_CONTENT_SENDERS_NONE,
  WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR,
  WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER,
  WOCKY_JINGLE_CONTENT_SENDERS_BOTH
} WockyJingleContentSenders;

typedef enum { /*< skip >*/
  JINGLE_TRANSPORT_UNKNOWN,
  JINGLE_TRANSPORT_GOOGLE_P2P,
  JINGLE_TRANSPORT_RAW_UDP,
  JINGLE_TRANSPORT_ICE_UDP,
} WockyJingleTransportType;

typedef enum { /*< skip >*/
  WOCKY_JINGLE_TRANSPORT_PROTOCOL_UDP,
  WOCKY_JINGLE_TRANSPORT_PROTOCOL_TCP
} WockyJingleTransportProtocol;

typedef enum { /*< skip >*/
  WOCKY_JINGLE_CANDIDATE_TYPE_LOCAL,
  WOCKY_JINGLE_CANDIDATE_TYPE_STUN,
  WOCKY_JINGLE_CANDIDATE_TYPE_RELAY
} WockyJingleCandidateType;

typedef enum
{
  WOCKY_JINGLE_REASON_UNKNOWN,
  WOCKY_JINGLE_REASON_ALTERNATIVE_SESSION,
  WOCKY_JINGLE_REASON_BUSY,
  WOCKY_JINGLE_REASON_CANCEL,
  WOCKY_JINGLE_REASON_CONNECTIVITY_ERROR,
  WOCKY_JINGLE_REASON_DECLINE,
  WOCKY_JINGLE_REASON_EXPIRED,
  WOCKY_JINGLE_REASON_FAILED_APPLICATION,
  WOCKY_JINGLE_REASON_FAILED_TRANSPORT,
  WOCKY_JINGLE_REASON_GENERAL_ERROR,
  WOCKY_JINGLE_REASON_GONE,
  WOCKY_JINGLE_REASON_INCOMPATIBLE_PARAMETERS,
  WOCKY_JINGLE_REASON_MEDIA_ERROR,
  WOCKY_JINGLE_REASON_SECURITY_ERROR,
  WOCKY_JINGLE_REASON_SUCCESS,
  WOCKY_JINGLE_REASON_TIMEOUT,
  WOCKY_JINGLE_REASON_UNSUPPORTED_APPLICATIONS,
  WOCKY_JINGLE_REASON_UNSUPPORTED_TRANSPORTS
} WockyJingleReason;


#endif /* GABBLE_JINGLE_ENUMS_H */
