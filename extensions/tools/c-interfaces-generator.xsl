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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
-->

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <xsl:output method="text" indent="no" encoding="ascii"/>

  <xsl:variable name="upper" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
  <xsl:variable name="lower" select="'abcdefghijklmnopqrstuvwxyz'"/>

  <xsl:template match="interface">
    <xsl:text>/**&#10; * TP_IFACE_</xsl:text>
    <xsl:value-of select="translate(../@name, concat($lower, '/'), $upper)"/>
    <xsl:text>:&#10; * &#10; * The Telepathy interface name "</xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>"&#10; */&#10;#define TP_IFACE_</xsl:text>
    <xsl:value-of select="translate(../@name, concat($lower, '/'), $upper)"/>
    <xsl:text> \&#10;"</xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>"&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="text()"/>

  <xsl:template match="/tp:spec">/* Generated from the Telepathy spec

<xsl:for-each select="tp:copyright">
<xsl:value-of select="."/><xsl:text>
</xsl:text></xsl:for-each><xsl:text>
</xsl:text><xsl:value-of select="tp:license"/>
<xsl:value-of select="tp:docstring"/>
*/

<xsl:apply-templates select="node"/>
</xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et noai noci: -->
