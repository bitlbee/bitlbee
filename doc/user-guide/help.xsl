<?xml version='1.0'?>
<!--
	Convert DocBook documentation to help.txt file used by bitlbee
	(C) 2004 Jelmer Vernooij
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	version="1.1">

	<xsl:output method="text" encoding="utf-8" standalone="yes"/>
	<xsl:strip-space elements="*"/>

	<xsl:template match="text()">
		<xsl:if test="starts-with(.,' ') and preceding-sibling::* and
			not(preceding-sibling::*[1]/node()[1][self::text() and contains(concat(.,'^$%'),' ^$%')])">
			<xsl:text> </xsl:text>
		</xsl:if>
	
		<xsl:value-of select="normalize-space(.)"/>
		<xsl:if test="contains(concat(.,'^$%'),' ^$%') and following-sibling::* and
			not(following-sibling::*[1]/node()[1][self::text() and starts-with(.,' ')])">
			<xsl:text> </xsl:text>
		</xsl:if>
	</xsl:template>

	<xsl:template match="para">
		<xsl:apply-templates/><xsl:text>&#10;</xsl:text>
		<xsl:if test="$extraparanewline = '1'">
			<xsl:text>&#10;</xsl:text>
		</xsl:if>
	</xsl:template>

	<xsl:template name="subject">
		<xsl:param name="id"/>
		<xsl:message><xsl:text>Processing: </xsl:text><xsl:value-of select="$id"/></xsl:message>
		<xsl:text>?</xsl:text><xsl:value-of select="$id"/><xsl:text>&#10;</xsl:text>

		<xsl:for-each select="para|variablelist|simplelist|command-list|ircexample">
			<xsl:if test="title != ''">
				<xsl:value-of select="title"/><xsl:text>&#10;</xsl:text>
			</xsl:if>
			<xsl:apply-templates select="."/>
		</xsl:for-each>
		<xsl:text>%&#10;</xsl:text>

		<xsl:for-each select="sect1|sect2">
			<xsl:call-template name="subject">
				<xsl:with-param name="id" select="@id"/>
			</xsl:call-template>
		</xsl:for-each>

		<xsl:for-each select="bitlbee-command">
			<xsl:call-template name="cmd">
				<xsl:with-param name="prefix" select="''"/>
			</xsl:call-template>
		</xsl:for-each>

		<xsl:for-each select="bitlbee-setting">
			<xsl:message><xsl:text>Processing setting '</xsl:text><xsl:value-of select="@name"/><xsl:text>'</xsl:text></xsl:message>
			<xsl:text>?set </xsl:text><xsl:value-of select="@name"/><xsl:text>&#10;</xsl:text>
			<xsl:text>_b_Type:_b_ </xsl:text><xsl:value-of select="@type"/><xsl:text>&#10;</xsl:text>
			<xsl:text>_b_Scope:_b_ </xsl:text><xsl:value-of select="@scope"/><xsl:text>&#10;</xsl:text>
			<xsl:if test="default">
				<xsl:text>_b_Default:_b_ </xsl:text><xsl:value-of select="default"/><xsl:text>&#10;</xsl:text>
			</xsl:if>
			<xsl:if test="possible-values">
				<xsl:text>_b_Possible Values:_b_ </xsl:text><xsl:value-of select="possible-values"/><xsl:text>&#10;</xsl:text>
			</xsl:if>
			<xsl:text>&#10;</xsl:text>
			<xsl:apply-templates select="description"/>
			<xsl:text>%&#10;</xsl:text>
		</xsl:for-each>
	</xsl:template>

	<xsl:template match="command-list">
		<xsl:for-each select="../bitlbee-command">
			<xsl:text> * _b_</xsl:text><xsl:value-of select="@name"/><xsl:text>_b_ - </xsl:text><xsl:value-of select="short-description"/><xsl:text>&#10;</xsl:text>
		</xsl:for-each>
		<xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="preface|chapter|sect1|sect2">
		<xsl:call-template name="subject">
			<xsl:with-param name="id" select="@id"/>
		</xsl:call-template>
	</xsl:template>

	<xsl:template match="emphasis">
		<xsl:text>_b_</xsl:text>
		<xsl:apply-templates/>
		<xsl:text>_b_</xsl:text>
	</xsl:template>

	<xsl:template match="book">
		<xsl:apply-templates/>
	</xsl:template>

	<xsl:template match="variablelist">
		<xsl:for-each select="varlistentry">
			<xsl:text> _b_</xsl:text><xsl:value-of select="term"/><xsl:text>_b_ - </xsl:text><xsl:value-of select="listitem/para"/><xsl:text>&#10;</xsl:text>
		</xsl:for-each>
		<xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="simplelist">
		<xsl:for-each select="member">
			<xsl:text> - </xsl:text><xsl:apply-templates/><xsl:text>&#10;</xsl:text>
		</xsl:for-each>
		<xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="ircline">
		<xsl:text>_b_&lt;</xsl:text><xsl:value-of select="@nick"/><xsl:text>&gt;_b_ </xsl:text><xsl:value-of select="."/><xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="ircaction">
		<xsl:text>_b_* </xsl:text><xsl:value-of select="@nick"/><xsl:text>_b_ </xsl:text><xsl:value-of select="."/><xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="ircexample">
		<xsl:apply-templates/>
		<xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template name="cmd">
		<xsl:param name="prefix"/>
		<xsl:variable name="thiscmd"><xsl:value-of select="$prefix"/><xsl:value-of select="@name"/></xsl:variable>
		<xsl:message><xsl:text>Processing command '</xsl:text><xsl:value-of select="$thiscmd"/><xsl:text>'</xsl:text></xsl:message>
		<xsl:text>?</xsl:text><xsl:value-of select="$thiscmd"/><xsl:text>&#10;</xsl:text>
		<xsl:for-each select="syntax">
			<xsl:text>_b_Syntax:_b_ </xsl:text><xsl:value-of select="."/><xsl:text>&#10;</xsl:text>
		</xsl:for-each>
		<xsl:text>&#10;</xsl:text>

		<xsl:apply-templates select="description"/>

		<xsl:for-each select="ircexample">
			<xsl:text>_b_Example:_b_&#10;</xsl:text>
			<xsl:apply-templates select="."/>
		</xsl:for-each>

		<!--
		<xsl:if test="bitlbee-command != ''">
			<xsl:text>Subcommands: </xsl:text>
			<xsl:for-each select="bitlbee-command">
				<xsl:value-of select="@name"/><xsl:text>, </xsl:text>
			</xsl:for-each>
			<xsl:text>&#10;</xsl:text>
		</xsl:if>
		-->

		<xsl:text>%&#10;</xsl:text>

		<xsl:for-each select="bitlbee-command">
			<xsl:call-template name="cmd">
				<xsl:with-param name="prefix"><xsl:value-of select="$thiscmd"/><xsl:text> </xsl:text></xsl:with-param>
			</xsl:call-template>
		</xsl:for-each>

	</xsl:template>

</xsl:stylesheet>
