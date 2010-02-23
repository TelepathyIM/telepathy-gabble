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

#define MAX_TYPES 14    /* increase as needed */
#define MAX_ELEMENTS 8  /* increase as needed */

typedef enum {
    /* in Telepathy: one value per field; in XMPP: one value per field */
    FIELD_SIMPLE,
    /* special case for NICKNAME */
    FIELD_NICKNAME,
    /* in Telepathy: exactly n_elements values; in XMPP: a child element for
     * each entry in elements, in that order */
    FIELD_STRUCTURED,
    /* same as FIELD_STRUCTURED but may not be repeated */
    FIELD_STRUCTURED_ONCE,
    /* same as FIELD_STRUCTURED except the last element may repeat n times */
    FIELD_REPEATING,
} FieldBehaviour;

typedef struct {
    const gchar *xmpp_name;
    const gchar *vcard_name;
    FieldBehaviour behaviour;
    GabbleContactInfoFieldFlags tp_flags;
    const gchar * const types[MAX_TYPES];
    const gchar * const elements[MAX_ELEMENTS];
} VCardField;

static VCardField known_fields[] = {
    /* Simple fields */
      { "FN", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "BDAY", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "MAILER", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "TZ", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "TITLE", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "ROLE", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "NOTE", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "PRODID", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "REV", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "SORT-STRING", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "UID", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "URL", NULL, FIELD_SIMPLE, 0, { NULL }, { NULL } },

    /* Simple fields which are Jabber-specific */
      { "JABBERID", "x-jabber", FIELD_SIMPLE, 0, { NULL }, { NULL } },
      { "DESC", "x-desc", FIELD_SIMPLE, 0, { NULL }, { NULL } },

    /* NICKNAME is special - multiple comma-separated values */
      { "NICKNAME", NULL, FIELD_NICKNAME, 0, { NULL }, { NULL } },

    /* Structured fields */
      { "N", NULL, FIELD_STRUCTURED_ONCE, 0, { NULL },
          { "FAMILY", "GIVEN", "MIDDLE", "PREFIX", "SUFFIX", NULL } },
      { "ADR", NULL, FIELD_STRUCTURED, 0,
          { "HOME", "WORK", "POSTAL", "PARCEL", "DOM", "INTL", "PREF", NULL },
          { "POBOX", "EXTADD", "STREET", "LOCALITY", "REGION", "PCODE", "CTRY",
            NULL } },
      { "GEO", NULL, FIELD_STRUCTURED_ONCE, 0,
          { NULL },
          { "LAT", "LON", NULL } },
      /* TEL and EMAIL are like structured fields: they have exactly one child
       * per occurrence */
      { "TEL", NULL, FIELD_STRUCTURED, 0,
          { "HOME", "WORK", "VOICE", "FAX", "PAGER", "MSG", "CELL", "VIDEO",
            "BBS", "MODEM", "ISDN", "PCS", "PREF", NULL },
          { "NUMBER", NULL } },
      { "EMAIL", NULL, FIELD_STRUCTURED, 0,
          { "HOME", "WORK", "INTERNET", "PREF", "X400", NULL },
          { "USERID", NULL } },

    /* Structured fields where the last element can repeat */
      { "LABEL", NULL, FIELD_REPEATING, 0,
          { "HOME", "WORK", "POSTAL", "PARCEL", "DOM", "INTL", "PREF", NULL },
          { "LINE", NULL } },
      { "ORG", NULL, FIELD_REPEATING, 0,
          { NULL },
          { "ORGNAME", "ORGUNIT", NULL } },

    /* Things we don't handle: */

      /* PHOTO: we treat it as the avatar instead */

      /* KEY: is Base64 (perhaps? hard to tell from the XEP) */
      /* LOGO: can be base64 or a URL */
      /* SOUND: can be base64, URL, or phonetic (!) */
      /* AGENT: is an embedded vCard (!) */
      /* CATEGORIES: same vCard encoding as NICKNAME, but split into KEYWORDs
       *  in XMPP; nobody is likely to use it on XMPP */
      /* CLASS: if you're putting non-PUBLIC vCards on your XMPP account,
       * you're probably Doing It Wrong */

      { NULL }
};
static GHashTable *known_fields_xmpp = NULL;
static GHashTable *known_fields_vcard = NULL;

static GPtrArray *supported_fields = NULL;

/*
 * _insert_contact_field:
 * @contact_info: an array of Contact_Info_Field structures
 * @field_name: a vCard field name in any case combination
 * @field_params: a list of vCard type-parameters, typically of the form
 *  type=xxx; must be in lower-case if case-insensitive
 * @field_values: for unstructured fields, an array containing one element;
 *  for structured fields, the elements of the field in order
 */
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
      const VCardField *field;

      if (!node->name || strcmp (node->name, "") == 0)
        continue;

      field = g_hash_table_lookup (known_fields_xmpp, node->name);

      if (field == NULL)
        {
          DEBUG ("unknown vCard node in XML: %s", node->name);
          continue;
        }

      switch (field->behaviour)
        {
        case FIELD_SIMPLE:
            {
              const gchar * const field_values[2] = {
                  lm_message_node_get_value (node),
                  NULL
              };

              _insert_contact_field (contact_info, node->name, NULL,
                  field_values);

            }
          break;

        case FIELD_STRUCTURED:
        case FIELD_STRUCTURED_ONCE:
        case FIELD_REPEATING:
          _create_contact_field_extended (contact_info, node,
              field->types, field->elements);
          break;

        case FIELD_NICKNAME:
            {
              const gchar *node_value = lm_message_node_get_value (node);

              /* we know that NICKNAME works like this now, and we can't handle
               * it any other way */
              g_assert (field->types[0] == NULL);
              g_assert (field->elements[0] == NULL);

              if (strchr (node_value, ','))
                {
                  GPtrArray *nicknames = g_ptr_array_new ();
                  const gchar *start, *p, *prev = NULL;
                  guint j;

                  start = p = node_value;
                  while (*p != '\0')
                    {
                      if (*p == ',' && (!prev || *prev != '\\'))
                        {
                          if ((p - start) != 0)
                            g_ptr_array_add (nicknames,
                                g_strndup (start, (p - start)));

                          start = (p + 1);
                        }

                      prev = p;
                      ++p;
                    }

                  if (start != p)
                    g_ptr_array_add (nicknames,
                        g_strndup (start, (p - start + 1)));

                  for (j = 0; j < nicknames->len; ++j)
                    {
                      const gchar * const field_values[2] = {
                          g_ptr_array_index (nicknames, j),
                          NULL
                      };

                      _insert_contact_field (contact_info, node->name,
                         NULL, field_values);
                    }

                  g_ptr_array_add (nicknames, NULL);
                  g_strfreev ((gchar **) g_ptr_array_free (nicknames, FALSE));
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
          break;

        default:
          g_assert_not_reached ();
        }
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

          /* we have the cached vcard but it cannot be parsed, skipping */
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

      if (vcard_error->domain == GABBLE_XMPP_ERROR)
        {
          switch (vcard_error->code)
            {
            case XMPP_ERROR_NOT_AUTHORIZED:
            case XMPP_ERROR_FORBIDDEN:
              tp_error.code = TP_ERROR_PERMISSION_DENIED;
              break;
            case XMPP_ERROR_ITEM_NOT_FOUND:
              tp_error.code = TP_ERROR_DOES_NOT_EXIST;
              break;
            }
          /* what other mappings make sense here? */
        }

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

static GSList *
_insert_edit_info (GSList *edits,
                   const VCardField *field,
                   const gchar * const * field_params,
                   const gchar * const * field_values)
{
  GabbleVCardManagerEditInfo *edit_info;
  const gchar * const * p;
  guint i;
  guint n_field_values = g_strv_length ((gchar **) field_values);
  guint n_elements = g_strv_length ((gchar **) field->elements);
  GabbleVCardEditType edit_type = GABBLE_VCARD_EDIT_APPEND;

  if (field->behaviour == FIELD_STRUCTURED_ONCE)
    edit_type = GABBLE_VCARD_EDIT_REPLACE;

  if (n_field_values != n_elements)
    {
      DEBUG ("Trying to edit %s field with wrong arguments", field->xmpp_name);
      return edits;
    }

  edit_info = gabble_vcard_manager_edit_info_new (field->xmpp_name, NULL,
      edit_type, NULL);
  edit_info->to_edit = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_free);

  for (p = field_params; *p != NULL; ++p)
    {
      /* all the type parameters that vcard-temp supports should be in the
       * format type=foo in Telepathy; in particular, we don't support
       * language=foo */
      if (!g_str_has_prefix (*p, "type="))
        continue;

      g_hash_table_insert (edit_info->to_edit,
          g_ascii_strup (*p + strlen ("type="), -1),
          NULL);
    }

  for (i = 0; i < n_elements; ++i)
    g_hash_table_insert (edit_info->to_edit, g_strdup (field->elements[i]),
        g_strdup (field_values[i]));

  return g_slist_append (edits, edit_info);
}

static void
_set_contact_info_cb (GabbleVCardManager *vcard_manager,
                      GabbleVCardManagerEditRequest *request,
                      LmMessageNode *vcard_node,
                      GError *vcard_error,
                      gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;

  if (NULL == vcard_node)
    {
      GError tp_error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          vcard_error->message };

      if (vcard_error->domain == GABBLE_XMPP_ERROR)
        if (vcard_error->code == XMPP_ERROR_BAD_REQUEST ||
            vcard_error->code == XMPP_ERROR_NOT_ACCEPTABLE)
          tp_error.code = TP_ERROR_INVALID_ARGUMENT;

      dbus_g_method_return_error (context, &tp_error);
    }
  else
    gabble_svc_connection_interface_contact_info_return_from_set_contact_info (
        context);
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
  GSList *edits = NULL;
  GPtrArray *nicknames = NULL;
  guint i;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  for (i = 0; i < contact_info->len; i++)
    {
      GValue contact_info_field = { 0, };
      gchar *field_name = NULL;
      gchar **field_params = NULL;
      gchar **field_values = NULL;
      guint n_field_values = 0;
      VCardField *field;

      g_value_init (&contact_info_field, GABBLE_STRUCT_TYPE_CONTACT_INFO_FIELD);
      g_value_set_static_boxed (&contact_info_field,
          g_ptr_array_index (contact_info, i));

      dbus_g_type_struct_get (&contact_info_field,
          0, &field_name,
          1, &field_params,
          2, &field_values,
          G_MAXUINT);

      if (field_values)
        n_field_values = g_strv_length (field_values);

      field = g_hash_table_lookup (known_fields_vcard, field_name);

      if (field == NULL)
        {
          DEBUG ("unknown vCard field from D-Bus: %s", field_name);
          continue;
        }

      switch (field->behaviour)
        {
        case FIELD_SIMPLE:
            {
              GabbleVCardManagerEditInfo *edit_info;
              gchar *tmp;

              if (n_field_values != 1)
                {
                  DEBUG ("Trying to edit %s field with wrong arguments",
                      field_name);
                  continue;
                }

              tmp = g_ascii_strup (field_name, -1);
              edit_info = gabble_vcard_manager_edit_info_new (tmp,
                  field_values[0], GABBLE_VCARD_EDIT_REPLACE, NULL);
              g_free (tmp);
              edits = g_slist_append (edits, edit_info);
            }
          break;

        case FIELD_STRUCTURED:
        case FIELD_STRUCTURED_ONCE:
        case FIELD_REPEATING:
          edits = _insert_edit_info (edits, field,
              (const gchar * const *) field_params,
              (const gchar * const *) field_values);
          break;

        case FIELD_NICKNAME:
            {
              if (n_field_values != 1)
                {
                  DEBUG ("Trying to edit %s field with wrong arguments",
                      field_name);
                  continue;
                }

              if (!nicknames)
                nicknames = g_ptr_array_new ();
              g_ptr_array_add (nicknames, g_strdup (field_values[0]));
            }
          break;

        default:
          g_assert_not_reached ();
        }

      g_free (field_name);
      g_strfreev (field_params);
      g_strfreev (field_values);
    }

  if (nicknames)
    {
      GabbleVCardManagerEditInfo *edit_info;

      edit_info = gabble_vcard_manager_edit_info_new ("NICKNAME",
          g_ptr_array_index (nicknames, 0), GABBLE_VCARD_EDIT_REPLACE, NULL);
      for (i = 1; i < nicknames->len; ++i)
        edit_info->element_value = g_strconcat (edit_info->element_value,
            ",", g_ptr_array_index (nicknames, i), NULL);
      edits = g_slist_append (edits, edit_info);

      g_ptr_array_add (nicknames, NULL);
      g_strfreev ((gchar **) g_ptr_array_free (nicknames, FALSE));
    }

  if (edits)
    gabble_vcard_manager_edit (self->vcard_manager, 0,
        _set_contact_info_cb, context,
        G_OBJECT (self), edits, TRUE);
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
  VCardField *field;

  /* These are never freed; they're only allocated once per run of Gabble.
   * The destructor in the latter is only set for completeness */
  known_fields_xmpp = g_hash_table_new (g_str_hash, g_str_equal);
  known_fields_vcard = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);

  supported_fields = dbus_g_type_specialized_construct (
          GABBLE_ARRAY_TYPE_FIELD_SPECS);

  for (field = known_fields; field->xmpp_name != NULL; field++)
    {
      GValueArray *va;
      gchar *vcard_name;

      if (field->vcard_name != NULL)
        vcard_name = g_strdup (field->vcard_name);
      else
        vcard_name = g_ascii_strdown (field->xmpp_name, -1);

      va = tp_value_array_build (4,
          G_TYPE_STRING, vcard_name,
          G_TYPE_STRV, NULL,            /* any type-param is allowed for now */
          G_TYPE_UINT, field->tp_flags,
          G_TYPE_UINT, G_MAXUINT32,     /* maximum occurrences */
          G_TYPE_INVALID);

      g_ptr_array_add (supported_fields, va);
      g_hash_table_insert (known_fields_xmpp,
          (gchar *) field->xmpp_name, field);
      g_hash_table_insert (known_fields_vcard, vcard_name, field);
    }
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
