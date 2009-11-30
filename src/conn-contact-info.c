/*
 * conn-contact-info.c - Gabble connection ContactInfo interface
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

#include "config.h"

#include "conn-contact-info.h"

#include <string.h>

#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/interfaces.h>

#include "extensions/extensions.h"

#include "vcard-manager.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "debug.h"
#include "util.h"

static GPtrArray *supported_fields = NULL;

static void
_insert_contact_field (GPtrArray *contact_info,
                       const gchar *field_name,
                       const gchar * const *field_params,
                       const gchar * const *field_values)
{
   GValue contact_info_field = { 0, };
   gchar *field_name_down = g_ascii_strdown (field_name, -1);

   g_value_init (&contact_info_field, GABBLE_STRUCT_TYPE_CONTACT_INFO_FIELD);
   g_value_take_boxed (&contact_info_field, dbus_g_type_specialized_construct (
       GABBLE_STRUCT_TYPE_CONTACT_INFO_FIELD));

   dbus_g_type_struct_set (&contact_info_field,
       0, field_name_down,
       1, field_params,
       2, field_values,
       G_MAXUINT);

   g_free (field_name_down);

   g_ptr_array_add (contact_info, g_value_get_boxed (&contact_info_field));
}

static void
_create_contact_field_extended (GPtrArray *contact_info,
                                LmMessageNode *node,
                                const gchar * const *supported_types,
                                const gchar * const *mandatory_fields)
{
  guint i;
  LmMessageNode *child_node;
  GPtrArray *field_params = NULL;
  gchar **field_values = NULL;
  guint supported_types_size = 0;
  guint mandatory_fields_size = 0;

  if (supported_types)
    supported_types_size = g_strv_length ((gchar **) supported_types);

  field_params = g_ptr_array_new ();

  /* we can simply omit a type if not found */
  for (i = 0; i < supported_types_size; ++i)
    {
       gchar *tmp;

       child_node = lm_message_node_get_child (node, supported_types[i]);
       if (child_node == NULL)
         continue;

       tmp = g_ascii_strdown (child_node->name, -1);
       g_ptr_array_add (field_params, g_strdup_printf ("type=%s", tmp));
       g_free (tmp);
    }

  g_ptr_array_add (field_params, NULL);

  if (mandatory_fields)
    {
      mandatory_fields_size = g_strv_length ((gchar **) mandatory_fields);

      /* the mandatory field values need to be ordered properly */
      field_values = g_new0 (gchar *, mandatory_fields_size + 1);
      for (i = 0; i < mandatory_fields_size; ++i)
        {
           child_node = lm_message_node_get_child (node, mandatory_fields[i]);
           if (child_node != NULL)
             field_values[i] = (gchar *) lm_message_node_get_value (child_node);
           else
             field_values[i] = (gchar *) "";
        }
    }

  _insert_contact_field (contact_info, node->name,
      (const gchar * const *) field_params->pdata,
      (const gchar * const *) field_values);

  /* We allocated the strings in params, so need to free them */
  g_strfreev ((gchar **) g_ptr_array_free (field_params, FALSE));
  /* But we borrowed the ones in values, so just free the box */
  g_free (field_values);
}

static GPtrArray *
_parse_vcard (LmMessageNode *vcard_node,
              GError **error)
{
  GPtrArray *contact_info = dbus_g_type_specialized_construct (
      GABBLE_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST);
  NodeIter i;

  for (i = node_iter (vcard_node); i; i = node_iter_next (i))
    {
      LmMessageNode *node = node_iter_data (i);

      if (!node->name || strcmp (node->name, "") == 0)
        continue;

      if (strcmp (node->name, "FN") == 0 ||
          strcmp (node->name, "BDAY") == 0 ||
          strcmp (node->name, "JABBERID") == 0 ||
          strcmp (node->name, "MAILER") == 0 ||
          strcmp (node->name, "TZ") == 0 ||
          strcmp (node->name, "TITLE") == 0 ||
          strcmp (node->name, "ROLE") == 0 ||
          strcmp (node->name, "NOTE") == 0 ||
          strcmp (node->name, "PRODID") == 0 ||
          strcmp (node->name, "REV") == 0 ||
          strcmp (node->name, "SORT-STRING") == 0 ||
          strcmp (node->name, "UID") == 0 ||
          strcmp (node->name, "URL") == 0 ||
          strcmp (node->name, "DESC") == 0)
        {
          const gchar * const field_values[2] = {
              lm_message_node_get_value (node),
              NULL
          };

          _insert_contact_field (contact_info, node->name, NULL, field_values);
        }
     else if (strcmp (node->name, "N") == 0)
       {
          const gchar * const elements[] = { "FAMILY", "GIVEN", "MIDDLE",
              "PREFIX", "SUFFIX", NULL };

          _create_contact_field_extended (contact_info, node,
              NULL, elements);
       }
      else if (strcmp (node->name, "NICKNAME") == 0)
        {
          const gchar *node_value = lm_message_node_get_value (node);

          if (strchr (node_value, ','))
            {
              gchar **nicknames = g_strsplit (node_value, ",", -1);
              gchar **p;

              for (p = nicknames; *p != NULL; ++p)
                {
                  const gchar * const field_values[2] = {
                      *p,
                      NULL
                  };

                  _insert_contact_field (contact_info, node->name,
                     NULL, field_values);
                }

              g_strfreev (nicknames);
            }
          else
            {
              const gchar * const field_values[2] = {
                  node_value,
                  NULL
              };

              _insert_contact_field (contact_info, node->name,
                  NULL, field_values);
            }
        }
      else if (strcmp (node->name, "ADR") == 0)
        {
          const gchar * const types[] = { "HOME", "WORK", "POSTAL",
              "PARCEL", "DOM", "INTL", "PREF", NULL };
          const gchar * const elements[] = { "POBOX", "EXTADD", "STREET",
              "LOCALITY", "REGION", "PCODE", "CTRY", NULL };

          _create_contact_field_extended (contact_info, node,
              types, elements);
        }
      else if (strcmp (node->name, "LABEL") == 0)
        {
          const gchar * const types[] = { "HOME", "WORK", "POSTAL",
              "PARCEL", "DOM", "INTL", "PREF", NULL };
          const gchar * const elements[] = { "LINE", NULL };

          _create_contact_field_extended (contact_info, node,
              types, elements);
        }
      else if (strcmp (node->name, "TEL") == 0)
        {
          const gchar * const types[] = { "HOME", "WORK", "VOICE",
              "FAX", "PAGER", "MSG", "CELL", "VIDEO", "BBS", "MODEM", "ISDN",
              "PCS", "PREF", NULL };
          const gchar * const elements[] = { "NUMBER", NULL };

          _create_contact_field_extended (contact_info, node,
              types, elements);
        }
      else if (strcmp (node->name, "EMAIL") == 0)
        {
          const gchar * const types[] = { "HOME", "WORK", "INTERNET",
              "PREF", "X400", NULL };
          const gchar * const elements[] = { "USERID", NULL };

          _create_contact_field_extended (contact_info, node,
              types, elements);
        }
      else if (strcmp (node->name, "GEO") == 0)
        {
          const gchar * const elements[] = { "LAT", "LON", NULL };

          _create_contact_field_extended (contact_info, node,
              NULL, elements);
        }
      else if (strcmp (node->name, "ORG") == 0)
        {
          // TODO accept more than one ORGUNIT
          const gchar * const elements[] = { "ORGNAME", "ORGUNIT", NULL };

          _create_contact_field_extended (contact_info, node,
              NULL, elements);
        }
      else if (strcmp (node->name, "KEY") == 0)
        {
          const gchar * const types[] = { "TYPE", NULL };
          const gchar * const elements[] = { "CRED", NULL };

          _create_contact_field_extended (contact_info, node,
              types, elements);
        }

      // skipped fields
      // PHOTO
      // LOGO
      // AGENT
      // CATEGORIES
      // SOUND
      // CLASS
    }

  return contact_info;
}

static void
_emit_contact_info_changed (GabbleSvcConnectionInterfaceContactInfo *iface,
                            TpHandle contact,
                            LmMessageNode *vcard_node)
{
  GPtrArray *contact_info;

  if ((contact_info = _parse_vcard (vcard_node, NULL)) == NULL)
    return;

  gabble_svc_connection_interface_contact_info_emit_contact_info_changed (iface,
      contact, contact_info);

  g_boxed_free (GABBLE_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, contact_info);
}

static void
_request_vcards_cb (GabbleVCardManager *manager,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    LmMessageNode *vcard_node,
                    GError *vcard_error,
                    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleSvcConnectionInterfaceContactInfo *iface =
      (GabbleSvcConnectionInterfaceContactInfo *) conn;

  g_assert (g_hash_table_lookup (conn->vcard_requests,
      GUINT_TO_POINTER (handle)));

  g_hash_table_remove (conn->vcard_requests,
      GUINT_TO_POINTER (handle));

  if (vcard_error == NULL)
    _emit_contact_info_changed (iface, handle, vcard_node);
}

/**
 * gabble_connection_get_contact_info
 *
 * Implements D-Bus method GetContactInfo
 * on interface org.freedesktop.Telepathy.Connection.Interface.ContactInfo
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_contact_info (GabbleSvcConnectionInterfaceContactInfo *iface,
                                    const GArray *contacts,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contacts_repo =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;
  guint i;
  GHashTable *ret;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (TP_BASE_CONNECTION (iface),
      context);

  if (!tp_handles_are_valid (contacts_repo, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = dbus_g_type_specialized_construct (GABBLE_HASH_TYPE_CONTACT_INFO_MAP);

  for (i = 0; i < contacts->len; i++)
    {
      LmMessageNode *vcard_node;
      TpHandle contact = g_array_index (contacts, TpHandle, i);

      if (gabble_vcard_manager_get_cached (self->vcard_manager,
                                           contact, &vcard_node))
        {
          GPtrArray *contact_info;

          /* TODO what now? we have the cached vcard but it cannot be parsed,
           * skipping */
          if ((contact_info = _parse_vcard (vcard_node, NULL)) == NULL)
            {
              DEBUG ("contact %d vcard is cached but cannot be parsed, "
                     "skipping.", contact);
              continue;
            }

          g_hash_table_insert (ret, GUINT_TO_POINTER (contact),
              contact_info);
        }
      else
        {
          if (g_hash_table_lookup (self->vcard_requests,
                                   GUINT_TO_POINTER (contact)) == NULL)
            {
              GabbleVCardManagerRequest *request;

              request = gabble_vcard_manager_request (self->vcard_manager,
                contact, 0, _request_vcards_cb, self, NULL);

              g_hash_table_insert (self->vcard_requests,
                  GUINT_TO_POINTER (contact), request);
            }
        }
    }

  gabble_svc_connection_interface_contact_info_return_from_get_contact_info (
      context, ret);

  g_boxed_free (GABBLE_HASH_TYPE_CONTACT_INFO_MAP, ret);
}

static void
_return_from_request_contact_info (LmMessageNode *vcard_node,
                                   GError *vcard_error,
                                   DBusGMethodInvocation *context)
{
  GError *error = NULL;
  GPtrArray *contact_info;

  if (NULL == vcard_node)
    {
      GError tp_error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          vcard_error->message };

      dbus_g_method_return_error (context, &tp_error);
      return;
    }

  if ((contact_info = _parse_vcard (vcard_node, &error)) == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  gabble_svc_connection_interface_contact_info_return_from_request_contact_info (
      context, contact_info);

  g_boxed_free (GABBLE_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, contact_info);
}

static void
_request_vcard_cb (GabbleVCardManager *self,
                   GabbleVCardManagerRequest *request,
                   TpHandle handle,
                   LmMessageNode *vcard_node,
                   GError *vcard_error,
                   gpointer user_data)
{
  DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

  _return_from_request_contact_info (vcard_node, vcard_error, context);
}

/**
 * gabble_connection_request_contact_info
 *
 * Implements D-Bus method RequestContactInfo
 * on interface org.freedesktop.Telepathy.Connection.Interface.ContactInfo
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_request_contact_info (GabbleSvcConnectionInterfaceContactInfo *iface,
                                        guint contact,
                                        DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GError *err = NULL;
  LmMessageNode *vcard_node;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handle_is_valid (contact_handles, contact, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  if (gabble_vcard_manager_get_cached (self->vcard_manager,
                                       contact, &vcard_node))
    _return_from_request_contact_info (vcard_node, NULL, context);
  else
    gabble_vcard_manager_request (self->vcard_manager, contact, 0,
        _request_vcard_cb, context, NULL);
}

/**
 * gabble_connection_set_contact_info
 *
 * Implements D-Bus method SetContactInfo
 * on interface org.freedesktop.Telepathy.Connection.Interface.ContactInfo
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_set_contact_info (GabbleSvcConnectionInterfaceContactInfo *iface,
                                    const GPtrArray *contact_info,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  // TODO
}

static void
_vcard_updated (GObject *object,
                TpHandle contact,
                gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  LmMessageNode *vcard_node;

  if (gabble_vcard_manager_get_cached (conn->vcard_manager,
                                       contact, &vcard_node))
    _emit_contact_info_changed (
        GABBLE_SVC_CONNECTION_INTERFACE_CONTACT_INFO (conn),
        contact, vcard_node);
}

void
conn_contact_info_class_init (GabbleConnectionClass *klass)
{
  supported_fields = dbus_g_type_specialized_construct (
          GABBLE_ARRAY_TYPE_FIELD_SPECS);
}

void
conn_contact_info_init (GabbleConnection *conn)
{
  g_signal_connect (conn->vcard_manager, "vcard-update",
      G_CALLBACK (_vcard_updated), conn);
}

void
conn_contact_info_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcConnectionInterfaceContactInfoClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_contact_info_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_contact_info);
  IMPLEMENT(request_contact_info);
  IMPLEMENT(set_contact_info);
#undef IMPLEMENT
}

static TpDBusPropertiesMixinPropImpl props[] = {
      { "ContactInfoFlags", GUINT_TO_POINTER (GABBLE_CONTACT_INFO_FLAG_CAN_SET),
        NULL },
      { "SupportedFields", NULL, NULL },
      { NULL }
};
TpDBusPropertiesMixinPropImpl *conn_contact_info_properties = props;

void
conn_contact_info_properties_getter (GObject *object,
                                     GQuark interface,
                                     GQuark name,
                                     GValue *value,
                                     gpointer getter_data)
{
  GQuark q_supported_fields = g_quark_from_static_string (
      "SupportedFields");

  if (name == q_supported_fields)
    g_value_set_static_boxed (value, supported_fields);
  else
    g_value_set_uint (value, GPOINTER_TO_UINT (getter_data));
}
