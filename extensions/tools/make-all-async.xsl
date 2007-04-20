<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:output method="xml" indent="yes" encoding="ascii"/>

  <xsl:template match="method">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()"/>
      <xsl:if test="not(annotation[name='org.freedesktop.DBus.GLib.Async'])">
        <annotation name="org.freedesktop.DBus.GLib.Async" value=""/>
      </xsl:if>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et: -->
