/*
 * namespaces.h - XMPP namespace constants
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __GABBLE_NAMESPACES__H__
#define __GABBLE_NAMESPACES__H__

#define NS_AMP                  "http://jabber.org/protocol/amp"
#define NS_BYTESTREAMS          "http://jabber.org/protocol/bytestreams"
#define NS_CAPS                 "http://jabber.org/protocol/caps"
#define NS_CHAT_STATES          "http://jabber.org/protocol/chatstates"
#define NS_DISCO_INFO           "http://jabber.org/protocol/disco#info"
#define NS_DISCO_ITEMS          "http://jabber.org/protocol/disco#items"
#define NS_FEATURENEG           "http://jabber.org/protocol/feature-neg"
#define NS_FILE_TRANSFER        "http://jabber.org/protocol/si/profile/file-transfer"
#define NS_GABBLE_CAPS          "http://telepathy.freedesktop.org/caps"
#define NS_GOOGLE_CAPS          "http://www.google.com/xmpp/client/caps"
#define NS_GOOGLE_FEAT_SESSION  "http://www.google.com/xmpp/protocol/session"
#define NS_GOOGLE_FEAT_VOICE    "http://www.google.com/xmpp/protocol/voice/v1"
#define NS_GOOGLE_FEAT_VIDEO    "http://www.google.com/xmpp/protocol/video/v1"
#define NS_GOOGLE_JINGLE_INFO   "google:jingleinfo"
#define NS_GOOGLE_ROSTER        "google:roster"
#define NS_IBB                  "http://jabber.org/protocol/ibb"

/* Namespaces for XEP-0166 draft v0.15, the most capable Jingle dialect
 * supported by telepathy-gabble < 0.7.16, including the versions shipped with
 * Maemo Chinook and Diablo.
 */
#define NS_JINGLE015            "http://jabber.org/protocol/jingle"

/* RTP audio capability in Jingle v0.15 (obsoleted by NS_JINGLE_RTP) */
#define NS_JINGLE_DESCRIPTION_AUDIO \
  "http://jabber.org/protocol/jingle/description/audio"
/* RTP video capability in Jingle v0.15 (obsoleted by NS_JINGLE_RTP) */
#define NS_JINGLE_DESCRIPTION_VIDEO \
  "http://jabber.org/protocol/jingle/description/video"

/* XEP-0166 draft */
#define NS_JINGLE032            "urn:xmpp:jingle:1"
#define NS_JINGLE_ERRORS        "urn:xmpp:jingle:errors:1"

/* XEP-0167 (Jingle RTP) */
#define NS_JINGLE_RTP           "urn:xmpp:jingle:apps:rtp:1"
#define NS_JINGLE_RTP_ERRORS    "urn:xmpp:jingle:apps:rtp:errors:1"
#define NS_JINGLE_RTP_INFO      "urn:xmpp:jingle:apps:rtp:info:1"
#define NS_JINGLE_RTP_AUDIO     "urn:xmpp:jingle:apps:rtp:audio"
#define NS_JINGLE_RTP_VIDEO     "urn:xmpp:jingle:apps:rtp:video"

/* Google's Jingle dialect */
#define NS_GOOGLE_SESSION       "http://www.google.com/session"
/* Audio capability in Google Jingle dialect */
#define NS_GOOGLE_SESSION_PHONE "http://www.google.com/session/phone"
/* Video capability in Google's Jingle dialect */
#define NS_GOOGLE_SESSION_VIDEO "http://www.google.com/session/video"

/* google-p2p transport */
#define NS_GOOGLE_TRANSPORT_P2P "http://www.google.com/transport/p2p"
/* Jingle RAW-UDP transport */
#define NS_JINGLE_TRANSPORT_RAWUDP "urn:xmpp:jingle:transports:raw-udp:1"
/* Jingle ICE-UDP transport */
#define NS_JINGLE_TRANSPORT_ICEUDP "urn:xmpp:jingle:transports:ice-udp:1"

#define NS_MUC                  "http://jabber.org/protocol/muc"
#define NS_MUC_BYTESTREAM       "http://telepathy.freedesktop.org/xmpp/protocol/muc-bytestream"
#define NS_MUC_USER             "http://jabber.org/protocol/muc#user"
#define NS_MUC_ADMIN            "http://jabber.org/protocol/muc#admin"
#define NS_MUC_OWNER            "http://jabber.org/protocol/muc#owner"
#define NS_NICK                 "http://jabber.org/protocol/nick"
#define NS_OOB                  "jabber:iq:oob"
#define NS_OLPC_BUDDY_PROPS     "http://laptop.org/xmpp/buddy-properties"
#define NS_OLPC_ACTIVITIES      "http://laptop.org/xmpp/activities"
#define NS_OLPC_CURRENT_ACTIVITY    "http://laptop.org/xmpp/current-activity"
#define NS_OLPC_ACTIVITY_PROPS      "http://laptop.org/xmpp/activity-properties"
#define NS_OLPC_BUDDY           "http://laptop.org/xmpp/buddy"
#define NS_OLPC_ACTIVITY        "http://laptop.org/xmpp/activity"
#define NS_PUBSUB               "http://jabber.org/protocol/pubsub"
#define NS_PRESENCE_INVISIBLE   "presence-invisible"
#define NS_PRIVACY              "jabber:iq:privacy"
#define NS_REGISTER             "jabber:iq:register"
#define NS_ROSTER               "jabber:iq:roster"
#define NS_SI                   "http://jabber.org/protocol/si"
#define NS_SI_MULTIPLE          "http://telepathy.freedesktop.org/xmpp/si-multiple"
#define NS_TUBES                "http://telepathy.freedesktop.org/xmpp/tubes"
#define NS_VCARD_TEMP           "vcard-temp"
#define NS_VCARD_TEMP_UPDATE    "vcard-temp:x:update"
#define NS_X_DATA               "jabber:x:data"
#define NS_X_DELAY              "jabber:x:delay"
#define NS_X_CONFERENCE         "jabber:x:conference"
#define NS_XMPP_STANZAS         "urn:ietf:params:xml:ns:xmpp-stanzas"
#define NS_GEOLOC               "http://jabber.org/protocol/geoloc"


#endif /* __GABBLE_NAMESPACES__H__ */
