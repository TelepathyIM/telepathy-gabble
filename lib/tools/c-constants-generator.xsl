<!-- Stylesheet to extract C enumerations from the Telepathy spec.

Copyright (C) 2006, 2007 Collabora Limited

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
-->

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <xsl:output method="text" indent="no" encoding="ascii"/>

  <xsl:param name="mixed-case-prefix" select="'Tp'"/>
  <xsl:param name="upper-case-prefix" select="'TP_'"/>

  <xsl:variable name="upper" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
  <xsl:variable name="lower" select="'abcdefghijklmnopqrstuvwxyz'"/>

  <xsl:template match="tp:flags">
    <xsl:variable name="value-prefix">
      <xsl:choose>
        <xsl:when test="@value-prefix">
          <xsl:value-of select="@value-prefix"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
/* <xsl:value-of select="translate(concat($mixed-case-prefix, @name), '_', '')"/> (bitfield/set of flags, 0 for none) */
<xsl:if test="tp:docstring">/* <xsl:value-of select="tp:docstring"/> */</xsl:if>
typedef enum {
<xsl:apply-templates>
  <xsl:with-param name="value-prefix" select="$value-prefix"/>
</xsl:apply-templates>} <xsl:value-of select="translate(concat($mixed-case-prefix, @name), '_', '')"/>;

</xsl:template>

  <xsl:template match="tp:enum">
    <xsl:variable name="value-prefix">
      <xsl:choose>
        <xsl:when test="@value-prefix">
          <xsl:value-of select="@value-prefix"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
/* <xsl:value-of select="translate(concat($mixed-case-prefix, @name), '_', '')"/> (enum) */
<xsl:if test="tp:docstring">/* <xsl:value-of select="tp:docstring"/> */</xsl:if>
typedef enum {
<xsl:apply-templates>
  <xsl:with-param name="value-prefix" select="$value-prefix"/>
</xsl:apply-templates>    LAST_<xsl:value-of select="translate(concat($upper-case-prefix, $value-prefix), $lower, $upper)"/> = <xsl:value-of select="tp:enumvalue[position() = last()]/@value"/>
} <xsl:value-of select="translate(concat($mixed-case-prefix, @name), '_', '')"/>;

</xsl:template>

  <xsl:template match="tp:flags/tp:flag">
    <xsl:param name="value-prefix"/>
    <xsl:if test="@name or not(@suffix)">
      <xsl:message terminate="yes">Flag still has a name attr, or lacks suffix
</xsl:message>
    </xsl:if>

    <xsl:variable name="name" select="translate(concat($upper-case-prefix, $value-prefix, '_', @suffix), $lower, $upper)"/>
    <xsl:text>    </xsl:text><xsl:value-of select="$name"/> = <xsl:value-of select="@value"/>,
</xsl:template>

  <xsl:template match="tp:enum/tp:enumvalue">
    <xsl:param name="value-prefix"/>
    <xsl:if test="@name or not(@suffix)">
      <xsl:message terminate="yes">enumvalue has a name attr, or lacks suffix
</xsl:message>
    </xsl:if>

    <xsl:variable name="name" select="translate(concat($upper-case-prefix, $value-prefix, '_', @suffix), $lower, $upper)"/>

    <xsl:if test="preceding-sibling::tp:enumvalue and number(preceding-sibling::tp:enumvalue[1]/@value) > number(@value)">
      <xsl:message terminate="yes">Enum values must be in ascending numeric order,
but <xsl:value-of select="$name"/> is less than the previous value
</xsl:message>
    </xsl:if>

    <xsl:text>    </xsl:text><xsl:value-of select="$name"/> = <xsl:value-of select="@value"/>,
</xsl:template>

  <xsl:template match="tp:flag">
    <xsl:message terminate="yes">tp:flag found outside tp:flags
</xsl:message>
  </xsl:template>

  <xsl:template match="tp:enumvalue">
    <xsl:message terminate="yes">tp:enumvalue found outside tp:enum
</xsl:message>
  </xsl:template>

  <xsl:template match="text()"/>

  <xsl:template match="/tp:spec">/* Generated from the Telepathy spec, version <xsl:value-of select="tp:version"/><xsl:text>

</xsl:text><xsl:for-each select="tp:copyright">
      <xsl:value-of select="."/><xsl:text>
</xsl:text>
</xsl:for-each>
    <xsl:value-of select="tp:license"/><xsl:text>
</xsl:text><xsl:value-of select="tp:docstring"/>
*/

#ifdef __cplusplus
extern "C" {
#endif

<xsl:apply-templates select="node"/>

#ifdef __cplusplus
}
#endif

</xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et noai noci: -->
