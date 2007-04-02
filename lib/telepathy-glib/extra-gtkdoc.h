/* This file contains no code - it's just here for gtkdoc to pick up
 * documentation for otherwise undocumented generated files.
 *
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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

/**
 * SECTION:svc-properties-interface
 * @title: Service-side Properties interface
 * @short_description: GInterface for D-Bus objects exporting Telepathy
 *  properties
 * @see_also: #TpPropertiesMixin
 *
 * The Telepathy Properties interface associates a number of named properties
 * with a channel, connection or other D-Bus object. Signals are emitted
 * when the properties or their flags (readable/writable) change.
 */

/**
 * SECTION:svc-channel
 * @title: Service-side Channel types and interfaces
 * @short_description: GInterfaces for Telepathy Channel objects
 * @see_also: #TpChannelIface
 *
 * These interfaces (auto-generated from the Telepathy spec) make it easier
 * to export objects implementing the Telepathy Channel and its types and
 * optional interfaces, with the correct method and signal signatures,
 * and emit signals from those objects in a type-safe way.
 */

/**
 * SECTION:svc-connection
 * @title: Service-side Connection interfaces
 * @short_description: GInterfaces for Telepathy Connection objects
 * @see_also: #TpBaseConnection
 *
 * These interfaces (auto-generated from the Telepathy spec) make it easier
 * to export objects implementing the Telepathy Connection and its
 * optional interfaces, with the correct method and signal signatures,
 * and emit signals from those objects in a type-safe way.
 */

/**
 * SECTION:svc-connection-manager
 * @title: Service-side Connection Manager interface
 * @short_description: GInterface for Telepathy ConnectionManager objects
 * @see_also: #TpBaseConnection
 *
 * The #TpSvcConnectionManager interface (auto-generated from the Telepathy
 * spec) makes it easier to export an object implementing the Telepathy
 * ConnectionManager interface, with the correct method and signal signatures,
 * and emit signals from that object in a type-safe way.
 */

/**
 * SECTION:svc-media-interfaces
 * @title: Service-side media streaming helper interfaces
 * @short_description: media session and media stream
 * @see_also: #TpSvcChannelTypeStreamedMedia
 *
 * These interfaces (auto-generated from the telepathy spec) make it easier
 * to export the objects used to implement #TpSvcChannelTypeStreamedMedia,
 * with the correct method and signal signatures, and emit signals from those
 * objects.
 */

/**
 * SECTION:enums
 * @title: Telepathy protocol enumerations
 * @short_description: Enumerated types and bitfields from the Telepathy spec
 *
 * This header exposes the constants from the Telepathy specification as
 * C enums. It is automatically generated from the specification.
 *
 * The names used in the specification (e.g.
 * Connection_Status_Connected) are converted to upper-case and given a
 * TP_ prefix, e.g. TP_CONNECTION_STATUS_CONNECTED.
 *
 * Each enum also has a constant for the number of members, named like
 * NUM_TP_CONNECTION_STATUSES. The pluralization is currently hard-coded
 * in the conversion scripts, but should move into the specification
 * in future.
 *
 * Constants LAST_TP_CONNECTION_STATUS, etc. are also provided. These are
 * deprecated and will be removed in a future release.
 */

/**
 * SECTION:interfaces
 * @title: Telepathy protocol interface strings
 * @short_description: D-Bus interface names from the Telepathy spec
 *
 * This header exposes the interface names from the Telepathy specification
 * as cpp defines for strings. It is automatically generated from the
 * specification.
 */

/**
 * SECTION:errors
 * @title: Telepathy protocol errors
 * @short_description: The errors from the Telepathy D-Bus spec, as a
 *  GLib error domain
 *
 * This header provides the Telepathy D-Bus errors, in the form of a
 * GLib error domain. For D-Bus methods which fail with one of these errors,
 * dbus-glib will generate a reply message with the appropriate error.
 *
 * It also provides utility functions used by functions which return an error.
 */

/**
 * SECTION:handle
 * @title: TpHandle
 * @short_description: type representing handles
 * @see_also: TpHandleRepoIface
 *
 * The TpHandle type represents a Telepathy handle.
 */
