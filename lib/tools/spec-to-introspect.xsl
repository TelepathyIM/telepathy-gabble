<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <xsl:template match="*">
    <xsl:copy>
      <xsl:for-each select="@*">
        <xsl:if test="not(starts-with(name(), 'tp:'))">
          <xsl:copy/>
        </xsl:if>
      </xsl:for-each>
      <xsl:apply-templates/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="tp:*"/>
  <xsl:template match="text()"/>

  <xsl:output method="xml" indent="yes" encoding="UTF-8"
    omit-xml-declaration="no"
    doctype-system="http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd"
    doctype-public="-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" />

</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et: -->
