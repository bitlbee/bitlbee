<?xml version='1.0'?>
<!--
	Convert BitlBee XML documentation to DocBook
	(C) 2004 Jelmer Vernooij
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns:exsl="http://exslt.org/common"
	version="1.1"
	extension-element-prefixes="exsl">

	<xsl:output method="xml" encoding="UTF-8" doctype-public="-//OASIS//DTD DocBook XML V4.2//EN" indent="yes" doctype-system="http://www.oasis-open.org/docbook/xml/4.2/docbookx.dtd"/>

	<xsl:strip-space elements="*"/>

	<xsl:template match="ircline">
		<xsl:element name="prompt"><xsl:text>&lt; </xsl:text><xsl:value-of select="@nick"/><xsl:text>&gt; </xsl:text></xsl:element>
		<xsl:element name="userinput"><xsl:value-of select="normalize-space(.)"/></xsl:element><xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="ircaction">
		<xsl:text> * </xsl:text><xsl:value-of select="@nick"/><xsl:value-of select="normalize-space(.)"/><xsl:text>&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="ircexample">
		<xsl:element name="screen">
			<xsl:for-each select="ircline|ircaction">
				<xsl:apply-templates select="."/>
			</xsl:for-each>
		</xsl:element>
	</xsl:template>


	<!-- This is needed to copy content unchanged -->
	<xsl:template match="@*|node()">
		<xsl:copy>
			<xsl:apply-templates select="@*|node()"/>
		</xsl:copy>
	</xsl:template>

	<xsl:template name="subcmd-list">
		<xsl:if test="bitlbee-command != ''">
			<xsl:element name="variablelist">
				<xsl:for-each select="bitlbee-command">
					<xsl:element name="varlistentry">
						<xsl:element name="term">
							<xsl:value-of select="@name"/>
						</xsl:element>
						<xsl:element name="listitem">
							<xsl:element name="para">
								<xsl:value-of select="short-description"/>
							</xsl:element>
						</xsl:element>
					</xsl:element>
				</xsl:for-each>
			</xsl:element>
		</xsl:if>
	</xsl:template>

	<xsl:template match="command-list">
		<xsl:call-template name="subcmd-list"/>
	</xsl:template>

	<xsl:template match="bitlbee-setting">
		<xsl:element name="sect1">
			<xsl:attribute name="id">
				<xsl:text>set_</xsl:text>
				<xsl:value-of select="@name"/>
			</xsl:attribute>
			<xsl:element name="title"><xsl:value-of select="@name"/></xsl:element>

			<xsl:element name="simplelist">
				<xsl:element name="member">
					<xsl:text>Type: </xsl:text><xsl:value-of select="@type"/>
				</xsl:element>
			</xsl:element>

			<xsl:for-each select="description/para">
				<xsl:apply-templates select="."/>
			</xsl:for-each>

		</xsl:element>
	</xsl:template>

	<xsl:template name="cmd">
		<xsl:variable name="thiscmd"><xsl:value-of select="$prefix"/><xsl:value-of select="@name"/></xsl:variable>
		<xsl:attribute name="id">
			<xsl:text>cmd_</xsl:text>
			<xsl:value-of select="translate($thiscmd,' ','_')"/>
		</xsl:attribute>
		<xsl:element name="title"><xsl:value-of select="$thiscmd"/>
			<xsl:if test="short-description">
				<xsl:text> - </xsl:text>
				<xsl:value-of select="short-description"/>
			</xsl:if>
		</xsl:element>

		<xsl:element name="formalpara">
			<xsl:element name="title"><xsl:text>Syntax:</xsl:text></xsl:element>
			<xsl:element name="para">
				<xsl:element name="programlisting">
					<xsl:for-each select="syntax">
						<xsl:value-of select="normalize-space(.)"/><xsl:text>&#10;</xsl:text>
					</xsl:for-each>
				</xsl:element>
			</xsl:element>
		</xsl:element>

		<xsl:for-each select="description/para">
			<xsl:apply-templates select="."/>
		</xsl:for-each>

		<xsl:for-each select="ircexample">
			<xsl:apply-templates select="."/>
		</xsl:for-each>

		<!--<xsl:call-template name="subcmd-list"/>-->

		<xsl:for-each select="bitlbee-command">
			<xsl:element name="sect2">
				<xsl:call-template name="cmd">
					<xsl:with-param name="prefix"><xsl:value-of select="$thiscmd"/><xsl:text> </xsl:text>
					</xsl:with-param>
				</xsl:call-template>
			</xsl:element>
		</xsl:for-each>
	</xsl:template>

	<xsl:template match="bitlbee-command">
		<xsl:element name="sect1">
			<xsl:call-template name="cmd">
				<xsl:with-param name="prefix" select="''"/>
			</xsl:call-template>
		</xsl:element>
	</xsl:template>

	</xsl:stylesheet>
