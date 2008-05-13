/*
 * caps-hash.c - Computing verification string hash (XEP-0115 v1.5)
 * Copyright (C) 2008 Collabora Ltd.
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

/* Computing verification string hash (XEP-0115 v1.5)
 *
 * Gabble does not do anything with dataforms (XEP-0128) included in
 * capabilities.  However, it needs to parse them in order to compute the hash
 * according to XEP-0115.
 */

#include "caps-hash.h"

#include <string.h>
#include <glib.h>
#include <time.h>

#include "base64.h"
#include "capabilities.h"
#include "presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "sha1/sha1.h"
#include "util.h"

#include "config.h"
#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE
#include "debug.h"

struct _dataform_field {
  gchar *fieldname;
  GPtrArray *values;
};

struct _dataform {
  gchar *form_type;
  GPtrArray *fields;
};


static gint
char_cmp (gconstpointer a, gconstpointer b)
{
  gchar *left = *(gchar **) a;
  gchar *right = *(gchar **) b;

  return strcmp (left, right);
}

static gint
fields_cmp (gconstpointer a, gconstpointer b)
{
  struct _dataform_field *left = *(struct _dataform_field **) a;
  struct _dataform_field *right = *(struct _dataform_field **) b;

  return strcmp (left->fieldname, right->fieldname);
}

static gint
dataforms_cmp (gconstpointer a, gconstpointer b)
{
  struct _dataform *left = *(struct _dataform **) a;
  struct _dataform *right = *(struct _dataform **) b;

  return strcmp (left->form_type, right->form_type);
}

static void
_free_field (gpointer data, gpointer user_data)
{
  struct _dataform_field *field = data;

  g_free (field->fieldname);
  g_ptr_array_foreach (field->values, (GFunc) g_free, NULL);

  g_slice_free1 (sizeof (struct _dataform_field), field);
}

static void
_free_form (gpointer data, gpointer user_data)
{
  struct _dataform *form = data;

  g_free (form->form_type);

  g_ptr_array_foreach (form->fields, _free_field, NULL);

  g_slice_free1 (sizeof (struct _dataform), form);
}

static void
gabble_presence_free_xep0115_hash (
    GPtrArray *features,
    GPtrArray *identities,
    GPtrArray *dataforms)
{
  g_ptr_array_foreach (features, (GFunc) g_free, NULL);
  g_ptr_array_foreach (identities, (GFunc) g_free, NULL);
  g_ptr_array_foreach (dataforms, _free_form, NULL);

  g_ptr_array_free (features, TRUE);
  g_ptr_array_free (identities, TRUE);
  g_ptr_array_free (dataforms, TRUE);
}

static gchar *
caps_hash_compute (
    GPtrArray *features,
    GPtrArray *identities,
    GPtrArray *dataforms)
{
  GString *s;
  gchar *str;
  gchar sha1[SHA1_HASH_SIZE];
  guint i;
  gchar *encoded;

  g_ptr_array_sort (identities, char_cmp);
  g_ptr_array_sort (features, char_cmp);
  g_ptr_array_sort (dataforms, dataforms_cmp);

  s = g_string_new ("");

  for (i = 0 ; i < identities->len ; i++)
    {
      g_string_append (s, g_ptr_array_index (identities, i));
      g_string_append_c (s, '<');
    }

  for (i = 0 ; i < features->len ; i++)
    {
      g_string_append (s, g_ptr_array_index (features, i));
      g_string_append_c (s, '<');
    }

  for (i = 0 ; i < dataforms->len ; i++)
    {
      guint j;
      struct _dataform *form = g_ptr_array_index (dataforms, i);

      g_assert (form->form_type != NULL);

      g_string_append (s, form->form_type);
      g_string_append_c (s, '<');

      g_ptr_array_sort (form->fields, fields_cmp);

      for (j = 0 ; j < form->fields->len ; j++)
        {
          guint k;
          struct _dataform_field *field = g_ptr_array_index (form->fields, j);

          g_string_append (s, field->fieldname);
          g_string_append_c (s, '<');

          g_ptr_array_sort (field->values, char_cmp);

          for (k = 0 ; k < field->values->len ; k++)
            {
              g_string_append (s, g_ptr_array_index (field->values, k));
              g_string_append_c (s, '<');
            }
        }
    }

  str = g_string_free (s, FALSE);
  DEBUG ("caps string: '%s'", str);
  sha1_bin (str, strlen (str), (guchar *) sha1);
  encoded = base64_encode (SHA1_HASH_SIZE, sha1, FALSE);
  DEBUG ("caps base64: '%s'", encoded);

  return encoded;
}

/**
 * Compute the hash as defined by the XEP-0115 from a received LmMessageNode
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
caps_hash_compute_from_lm_node (LmMessageNode *node)
{
  GPtrArray *features = g_ptr_array_new ();
  GPtrArray *identities = g_ptr_array_new ();
  GPtrArray *dataforms = g_ptr_array_new ();
  LmMessageNode *child;
  gchar *str;

  for (child = node->children; NULL != child; child = child->next)
    {
      if (g_str_equal (child->name, "identity"))
        {
          const gchar *category;
          const gchar *name;
          const gchar *type;
          const gchar *xmllang;

          category = lm_message_node_get_attribute (child, "category");
          name = lm_message_node_get_attribute (child, "name");
          type = lm_message_node_get_attribute (child, "type");
          xmllang = lm_message_node_get_attribute (child, "xml:lang");

          if (NULL == category)
            continue;
          if (NULL == name)
            name = "";
          if (NULL == type)
            type = "";
          if (NULL == xmllang)
            xmllang = "";

          g_ptr_array_add (identities,
              g_strdup_printf ("%s/%s/%s/%s", category, type, xmllang, name));
        }
      else if (g_str_equal (child->name, "feature"))
        {
          const gchar *var;
          var = lm_message_node_get_attribute (child, "var");

          if (NULL == var)
            continue;

          g_ptr_array_add (features, g_strdup (var));
        }
      else if (g_str_equal (child->name, "x"))
        {
          const gchar *xmlns;
          const gchar *type;
          LmMessageNode *x_child;
          struct _dataform *form;

          xmlns = lm_message_node_get_attribute (child, "xmlns");
          type = lm_message_node_get_attribute (child, "type");

          if (! g_str_equal (xmlns, "jabber:x:data"))
            continue;

          if (! g_str_equal (type, "result"))
            continue;

          form = g_slice_new0 (struct _dataform);
          form->form_type = NULL;
          form->fields = g_ptr_array_new ();

          for (x_child = child->children;
               NULL != x_child;
               x_child = x_child->next)
            {
              const gchar *var;
              LmMessageNode *value_child;

              if (! g_str_equal (x_child->name, "field"))
                continue;

              var = lm_message_node_get_attribute (x_child, "var");

              if (NULL == var)
                continue;

              if (g_str_equal (var, "FORM_TYPE"))
                {
                  for (value_child = x_child->children;
                       NULL != value_child;
                       value_child = value_child->next)
                    {
                      const gchar *content;

                      if (! g_str_equal (value_child->name, "value"))
                        continue;

                      content = lm_message_node_get_value (value_child);

                      /* If the stanza is correctly formed, there is only one
                       * FORM_TYPE and this check is useless. Otherwise, just
                       * use the first one */
                      if (form->form_type == NULL)
                        form->form_type = g_strdup (content);
                    }
                }
              else
                {
                  struct _dataform_field *field = NULL;

                  field = g_slice_new0 (struct _dataform_field);
                  field->values = g_ptr_array_new ();
                  field->fieldname = g_strdup (var);

                  for (value_child = x_child->children;
                       NULL != value_child;
                       value_child = value_child->next)
                    {
                      const gchar *content;

                      if (! g_str_equal (value_child->name, "value"))
                        continue;

                      content = lm_message_node_get_value (value_child);

                      g_ptr_array_add (field->values, g_strdup (content));
                    }

                    g_ptr_array_add (form->fields, (gpointer) field);
                }
            }

          /* this should not happen if the stanza is correctly formed. */
          if (form->form_type == NULL)
            form->form_type = g_strdup ("");

          g_ptr_array_add (dataforms, (gpointer) form);
        }
    }

  str = caps_hash_compute (features, identities, dataforms);

  gabble_presence_free_xep0115_hash (features, identities, dataforms);

  return str;
}


/**
 * Compute our hash as defined by the XEP-0115.
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
caps_hash_compute_from_self_presence (GabbleConnection *self)
{
  GabblePresence *presence = self->self_presence;
  GSList *features_list = capabilities_get_features (presence->caps);
  GPtrArray *features = g_ptr_array_new ();
  GPtrArray *identities = g_ptr_array_new ();
  GPtrArray *dataforms = g_ptr_array_new ();
  gchar *str;
  GSList *i;

  /* get our features list  */
  for (i = features_list; NULL != i; i = i->next)
    {
      const Feature *feat = (const Feature *) i->data;
      g_ptr_array_add (features, g_strdup (feat->ns));
    }

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  g_ptr_array_add (identities, g_strdup ("client/pc//" PACKAGE_STRING));

  /* Gabble does not use dataforms, let 'dataforms' be empty */

  str = caps_hash_compute (features, identities, dataforms);

  gabble_presence_free_xep0115_hash (features, identities, dataforms);
  g_slist_free (features_list);

  return str;
}

