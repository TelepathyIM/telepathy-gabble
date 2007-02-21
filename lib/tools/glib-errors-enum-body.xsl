<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <xsl:output method="text" indent="no" encoding="ascii"/>

  <xsl:variable name="upper" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
  <xsl:variable name="lower" select="'abcdefghijklmnopqrstuvwxyz'"/>

  <xsl:template match="tp:error">
    <!-- CHANNEL_BANNED -->
    <xsl:variable name="name" select="translate(@name, concat($lower, '. '),
                                                concat($upper, '__'))"/>
    <!-- Channel.Banned -->
    <xsl:variable name="nick" select="translate(@name, ' ', '')"/>
        /* <xsl:value-of select="concat(../@namespace, '.', $name)"/>
    <xsl:value-of select="tp:docstring"/> */
        { TP_ERROR_<xsl:value-of select="$name"/>, "TP_ERROR_<xsl:value-of select="$name"/>", "<xsl:value-of select="$nick"/>" },
</xsl:template>

  <xsl:template match="text()"/>

  <xsl:template match="/tp:errors">/* Generated from the Telepathy spec

<xsl:for-each select="tp:copyright">
<xsl:value-of select="."/><xsl:text>
</xsl:text></xsl:for-each><xsl:text>
</xsl:text><xsl:value-of select="tp:license"/>
*/

#include &lt;_gen/telepathy-errors.h&gt;

GType
tp_error_get_type (void)
{
  static GType etype = 0;
  if (G_UNLIKELY (etype == 0))
    {
      static const GEnumValue values[] = {
<xsl:apply-templates select="tp:error"/>      };

      etype = g_enum_register_static ("TpError", values);
    }
  return etype;
}

</xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et noai noci: -->
