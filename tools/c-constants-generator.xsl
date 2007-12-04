<!-- Stylesheet to extract C enumerations from the Telepathy spec.
The master copy of this stylesheet is in telepathy-glib - please make any
changes there.

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
-->

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <xsl:output method="text" indent="no" encoding="ascii"/>

  <xsl:param name="mixed-case-prefix" select="''"/>

  <xsl:variable name="upper" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
  <xsl:variable name="lower" select="'abcdefghijklmnopqrstuvwxyz'"/>

  <xsl:variable name="upper-case-prefix" select="concat(translate($mixed-case-prefix, $lower, $upper), '_')"/>
  <xsl:variable name="lower-case-prefix" select="concat(translate($mixed-case-prefix, $upper, $lower), '_')"/>


  <xsl:template match="tp:flags">
    <xsl:variable name="name">
      <xsl:choose>
        <xsl:when test="@plural">
          <xsl:value-of select="@plural"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="value-prefix">
      <xsl:choose>
        <xsl:when test="@singular">
          <xsl:value-of select="@singular"/>
        </xsl:when>
        <xsl:when test="@value-prefix">
          <xsl:value-of select="@value-prefix"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
/**
 * <xsl:value-of select="translate(concat($mixed-case-prefix, $name), '_', '')"/>:
<xsl:apply-templates mode="flag-or-enumvalue-gtkdoc">
  <xsl:with-param name="value-prefix" select="$value-prefix"/>
</xsl:apply-templates> *
<xsl:if test="tp:docstring">
 * &lt;![CDATA[<xsl:value-of select="translate(string (tp:docstring), '&#13;&#10;', '  ')"/>]]&gt;
 *
</xsl:if> * Bitfield/set of flags generated from the Telepathy specification.
 */
typedef enum {
<xsl:apply-templates>
  <xsl:with-param name="value-prefix" select="$value-prefix"/>
</xsl:apply-templates>} <xsl:value-of select="translate(concat($mixed-case-prefix, $name), '_', '')"/>;

</xsl:template>

  <xsl:template match="text()" mode="flag-or-enumvalue-gtkdoc"/>

  <xsl:template match="tp:enumvalue" mode="flag-or-enumvalue-gtkdoc">
    <xsl:param name="value-prefix"/>
    <xsl:text> * @</xsl:text>
    <xsl:value-of select="translate(concat($upper-case-prefix, $value-prefix, '_', @suffix), $lower, $upper)"/>
    <xsl:text>: &lt;![CDATA[</xsl:text>
    <xsl:value-of select="translate(string(tp:docstring), '&#13;&#10;', '  ')"/>
    <xsl:text>]]&gt;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="tp:flag" mode="flag-or-enumvalue-gtkdoc">
    <xsl:param name="value-prefix"/>
    <xsl:text> * @</xsl:text>
    <xsl:value-of select="translate(concat($upper-case-prefix, $value-prefix, '_', @suffix), $lower, $upper)"/>
    <xsl:text>: &lt;![CDATA[</xsl:text>
    <xsl:value-of select="translate(string(tp:docstring), '&#13;&#10;', '  ')"/>
    <xsl:text>]]&gt;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="tp:enum">
    <xsl:variable name="name">
      <xsl:choose>
        <xsl:when test="@singular">
          <xsl:value-of select="@singular"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="value-prefix">
      <xsl:choose>
        <xsl:when test="@singular">
          <xsl:value-of select="@singular"/>
        </xsl:when>
        <xsl:when test="@value-prefix">
          <xsl:value-of select="@value-prefix"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="name-plural">
      <xsl:choose>
        <xsl:when test="@plural">
          <xsl:value-of select="@plural"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/><xsl:text>s</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
/**
 * <xsl:value-of select="translate(concat($mixed-case-prefix, $name), '_', '')"/>:
<xsl:apply-templates mode="flag-or-enumvalue-gtkdoc">
  <xsl:with-param name="value-prefix" select="$value-prefix"/>
</xsl:apply-templates> *
<xsl:if test="tp:docstring">
  * &lt;![CDATA[[<xsl:value-of select="translate(string (tp:docstring), '&#13;&#10;', '  ')"/>]]&gt;
 *
</xsl:if> * Enumeration generated from the Telepathy specification.
 */
typedef enum {
<xsl:apply-templates>
  <xsl:with-param name="value-prefix" select="$value-prefix"/>
</xsl:apply-templates>} <xsl:value-of select="translate(concat($mixed-case-prefix, $name), '_', '')"/>;

/**
 * NUM_<xsl:value-of select="translate(concat($upper-case-prefix, $name-plural), $lower, $upper)"/>:
 *
 * 1 higher than the highest valid value of #<xsl:value-of select="translate(concat($mixed-case-prefix, $name), '_', '')"/>.
 */
#define NUM_<xsl:value-of select="translate(concat($upper-case-prefix, $name-plural), $lower, $upper)"/> (<xsl:value-of select="tp:enumvalue[position() = last()]/@value"/>+1)

</xsl:template>

  <xsl:template match="tp:flags/tp:flag">
    <xsl:param name="value-prefix"/>
    <xsl:variable name="suffix">
      <xsl:choose>
        <xsl:when test="@suffix">
          <xsl:value-of select="@suffix"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="name" select="translate(concat($upper-case-prefix, $value-prefix, '_', $suffix), $lower, $upper)"/>

    <xsl:if test="@name and @suffix and @name != @suffix">
      <xsl:message terminate="yes">Flag name <xsl:value-of select="@name"/> != suffix <xsl:value-of select="@suffix"/>
</xsl:message>
    </xsl:if>
    <xsl:text>    </xsl:text><xsl:value-of select="$name"/> = <xsl:value-of select="@value"/>,
</xsl:template>

  <xsl:template match="tp:enum/tp:enumvalue">
    <xsl:param name="value-prefix"/>
    <xsl:variable name="suffix">
      <xsl:choose>
        <xsl:when test="@suffix">
          <xsl:value-of select="@suffix"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="@name"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="name" select="translate(concat($upper-case-prefix, $value-prefix, '_', $suffix), $lower, $upper)"/>

    <xsl:if test="@name and @suffix and @name != @suffix">
      <xsl:message terminate="yes">Flag name <xsl:value-of select="@name"/> != suffix <xsl:value-of select="@suffix"/>
</xsl:message>
    </xsl:if>

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

  <xsl:template match="/tp:spec">
    <xsl:if test="$mixed-case-prefix = ''">
      <xsl:message terminate="yes">
        <xsl:text>mixed-case-prefix param must be set&#10;</xsl:text>
      </xsl:message>
    </xsl:if>

    <xsl:text>/* Generated from </xsl:text>
    <xsl:value-of select="tp:title"/>
    <xsl:if test="tp:version">
      <xsl:text>, version </xsl:text>
      <xsl:value-of select="tp:version"/>
    </xsl:if>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:for-each select="tp:copyright">
      <xsl:value-of select="."/>
      <xsl:text>&#10;</xsl:text>
    </xsl:for-each>
    <xsl:value-of select="tp:license"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:value-of select="tp:docstring"/>
    <xsl:text>&#10; */

#ifdef __cplusplus
extern "C" {
#endif

</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>

#ifdef __cplusplus
}
#endif

</xsl:text>
  </xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et noai noci: -->
