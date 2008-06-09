<!-- Stylesheet to extract C interface names from the Telepathy spec.
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
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
-->

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <xsl:import href="c-interfaces-generator.xsl"/>

  <xsl:template match="interface">
    <xsl:apply-imports/>

    <xsl:text>/**&#10; * </xsl:text>
    <xsl:value-of select="$PREFIX"/>
    <xsl:text>_IFACE_QUARK_</xsl:text>
    <xsl:value-of select="translate(../@name, concat($lower, '/'), $upper)"/>
    <xsl:text>:&#10; * &#10; * Expands to a call to a function that </xsl:text>
    <xsl:text>returns a quark for the interface name "</xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>"&#10; */&#10;#define </xsl:text>
    <xsl:value-of select="$PREFIX"/>
    <xsl:text>_IFACE_QUARK_</xsl:text>
    <xsl:value-of select="translate(../@name, concat($lower, '/'), $upper)"/>
    <xsl:text> \&#10;  (</xsl:text>
    <xsl:value-of select="$prefix"/>
    <xsl:text>_iface_quark_</xsl:text>
    <xsl:value-of select="translate(../@name, concat($upper, '/'), $lower)"/>
    <xsl:text> ())&#10;&#10;GQuark </xsl:text>
    <xsl:value-of select="$prefix"/>
    <xsl:text>_iface_quark_</xsl:text>
    <xsl:value-of select="translate(../@name, concat($upper, '/'), $lower)"/>
    <xsl:text> (void);&#10;&#10;</xsl:text>
  </xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et noai noci: -->
