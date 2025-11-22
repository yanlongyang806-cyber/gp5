/*
	Hello, and welcome to a terrible file!

	Writing XML for excel to read necessitates a couple of long, ugly format strings. 
	 A few things are hardcoded, such as the "Style" block (since I wanted all reports to look
	 basically the same) and the document header.

	To re-generate any of these strings, take the preceding block comment and find-replace " with \" and \n with \\n.

	Some of them have printf tokens embedded.

	 - C. Miller 7/11/2013
*/

const char* pchDocumentHeader = "<?xml version=\"1.0\"?>\n	<?mso-application progid=\"Excel.Sheet\"?>\n	<Workbook\n	xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\"\nxmlns:o=\"urn:schemas-microsoft-com:office:office\"\nxmlns:x=\"urn:schemas-microsoft-com:office:excel\"\nxmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\"\nxmlns:html=\"http://www.w3.org/TR/REC-html40\">\n	  <DocumentProperties xmlns=\"urn:schemas-microsoft-com:office:office\">\n	  <Company>ORGANIZATION</Company>\n	  </DocumentProperties>\n	  <ExcelWorkbook xmlns=\"urn:schemas-microsoft-com:office:excel\">\n	  <WindowHeight>6795</WindowHeight>\n	  <WindowWidth>8460</WindowWidth>\n	  <WindowTopX>120</WindowTopX>\n	  <WindowTopY>15</WindowTopY>\n	  <ProtectStructure>False</ProtectStructure>\n	  <ProtectWindows>False</ProtectWindows>\n	  </ExcelWorkbook>";

/*
<Styles>
	<Style ss:ID="Default" ss:Name="Normal">
		<Alignment ss:Vertical="Bottom" ss:Horizontal="Left" />
		<Font x:Family="Swiss" ss:Size="10" ss:Color="Black" />
		<Borders>
			<Border ss:Color="LightGray" ss:Position="Left" ss:Weight="1" ss:LineStyle="Continuous"/>
			<Border ss:Color="LightGray" ss:Position="Right" ss:Weight="1" ss:LineStyle="Continuous"/>
		</Borders>
		</Style>
	<Style ss:ID="DefaultRightAligned">
		<Alignment ss:Horizontal="Right"/>
		<NumberFormat ss:Format="#,##0"/>  
		</Style>
	<Style ss:ID="Currency">
		<NumberFormat ss:Format="$#,##0.00"/>
	</Style>
	<Style ss:ID="CurrencyRightAligned">
		<Alignment ss:Horizontal="Right"/>
		<NumberFormat ss:Format="$#,##0.00"/>
	</Style>
	<Style ss:ID="GreyCurrency">
		<Interior ss:Color="LightGray" ss:Pattern="Solid"/>
		<NumberFormat ss:Format="$#,##0.00"/>
	</Style>
	<Style ss:ID="GreyCurrencyRightAligned">
		<Interior ss:Color="LightGray" ss:Pattern="Solid"/>
		<Alignment ss:Horizontal="Right"/>
		<NumberFormat ss:Format="$#,##0.00"/>
	</Style>
	<Style ss:ID="GreyDefault">
		<Interior ss:Color="LightGray" ss:Pattern="Solid"/>
	</Style>
	<Style ss:ID="GreyDefaultRightAligned">
		<Alignment ss:Horizontal="Right"/>
		<Interior ss:Color="LightGray" ss:Pattern="Solid"/>
		<NumberFormat ss:Format="#,##0"/> 
	</Style>
	<Style ss:ID="Bold">
		<Font x:Family="Swiss" ss:Bold="1" />
	</Style>
	<Style ss:ID="BigBold">
		<Font x:Family="Swiss" ss:Bold="1" ss:Size="16" />
	</Style>
	<Style ss:ID="HugeBold">
		<Font x:Family="Swiss" ss:Bold="1" ss:Size="24" />
	</Style>
	<Style ss:ID="ColumnHeaderDefault">
		<Font x:Family="Swiss" ss:Bold="1" ss:Size="12" ss:Color="White" />
		<Interior ss:Color="RoyalBlue" ss:Pattern="Solid"/>
		<Alignment ss:WrapText="1"/>
	</Style>
	<Style ss:ID="ColumnHeaderDefaultRightAligned" ss:Parent="ColumnHeaderDefault">
		<Alignment ss:WrapText="1" ss:Horizontal="Right"/>
	</Style>
</Styles>
*/
const char* pchStyleBlock = "<Styles>\n	<Style ss:ID=\"Default\" ss:Name=\"Normal\">\n	<Alignment ss:Vertical=\"Bottom\" ss:Horizontal=\"Left\" />\n	<Font x:Family=\"Swiss\" ss:Size=\"10\" ss:Color=\"Black\" />\n	<Borders>\n	<Border ss:Color=\"LightGray\" ss:Position=\"Left\" ss:Weight=\"1\" ss:LineStyle=\"Continuous\"/>\n	<Border ss:Color=\"LightGray\" ss:Position=\"Right\" ss:Weight=\"1\" ss:LineStyle=\"Continuous\"/>\n	</Borders>\n	</Style>\n	<Style ss:ID=\"DefaultRightAligned\">\n	<Alignment ss:Horizontal=\"Right\"/>\n	<NumberFormat ss:Format=\"#,##0\"/>  \n	</Style>\n	<Style ss:ID=\"Currency\">\n	<NumberFormat ss:Format=\"$#,##0.00\"/>\n	</Style>\n	<Style ss:ID=\"CurrencyRightAligned\">\n	<Alignment ss:Horizontal=\"Right\"/>\n	<NumberFormat ss:Format=\"$#,##0.00\"/>\n	</Style>\n	<Style ss:ID=\"GreyCurrency\">\n	<Interior ss:Color=\"LightGray\" ss:Pattern=\"Solid\"/>\n	<NumberFormat ss:Format=\"$#,##0.00\"/>\n	</Style>\n	<Style ss:ID=\"GreyCurrencyRightAligned\">\n	<Interior ss:Color=\"LightGray\" ss:Pattern=\"Solid\"/>\n	<Alignment ss:Horizontal=\"Right\"/>\n	<NumberFormat ss:Format=\"$#,##0.00\"/>\n	</Style>\n	<Style ss:ID=\"GreyDefault\">\n	<Interior ss:Color=\"LightGray\" ss:Pattern=\"Solid\"/>\n	</Style>\n	<Style ss:ID=\"GreyDefaultRightAligned\">\n	<Alignment ss:Horizontal=\"Right\"/>\n	<Interior ss:Color=\"LightGray\" ss:Pattern=\"Solid\"/>\n	<NumberFormat ss:Format=\"#,##0\"/> \n	</Style>\n	<Style ss:ID=\"Bold\">\n	<Font x:Family=\"Swiss\" ss:Bold=\"1\" />\n	</Style>\n	<Style ss:ID=\"BigBold\">\n	<Font x:Family=\"Swiss\" ss:Bold=\"1\" ss:Size=\"16\" />\n	</Style>\n	<Style ss:ID=\"HugeBold\">\n	<Font x:Family=\"Swiss\" ss:Bold=\"1\" ss:Size=\"24\" />\n	</Style>\n	<Style ss:ID=\"ColumnHeaderDefault\">\n	<Font x:Family=\"Swiss\" ss:Bold=\"1\" ss:Size=\"12\" ss:Color=\"White\" />\n	<Interior ss:Color=\"RoyalBlue\" ss:Pattern=\"Solid\"/>\n	<Alignment ss:WrapText=\"1\"/>\n	</Style>\n	<Style ss:ID=\"ColumnHeaderDefaultRightAligned\" ss:Parent=\"ColumnHeaderDefault\">\n	<Alignment ss:WrapText=\"1\" ss:Horizontal=\"Right\"/>\n	</Style>\n	</Styles>";

/*
<Worksheet ss:Name="%s">
	<Table ss:ExpandedColumnCount="%d" ss:ExpandedRowCount="%d"
	x:FullColumns="1" x:FullRows="1">
	%s
	%s
	</Table>
</Worksheet>
*/

//token list: sheet name (%s), num cols (%d), num rows (%d), column data (%s), row data (%s)
const char* pchWorksheetBlock = "<Worksheet ss:Name=\"%s\">\n	<Table ss:ExpandedColumnCount=\"%d\" ss:ExpandedRowCount=\"%d\"\nx:FullColumns=\"1\" x:FullRows=\"1\">\n  %s\n  %s\n  </Table>\n  </Worksheet>";

/*
<Column ss:Index="1" ss:Width="80" ss:ss:AutoFitWidth="1" ss:Span="%d" />
*/

//token list: num cols (%d)
const char* pchColumnBlock = "<Column ss:Index=\"1\" ss:Width=\"80\" ss:ss:AutoFitWidth=\"1\" ss:Span=\"%d\" />";

/*
<Row ss:Index="%d" ss:Height="%d">
	%s
</Row>
*/

//token list: row index (%d), Height (%d), block of all cell data (%s)
const char* pchRowBlock = "<Row ss:Index=\"%d\" ss:Height=\"%d\">\n	%s\n	</Row>";

/*
<Cell ss:Index="%d" ss:StyleID="%s">
	<Data ss:Type="Number">%llu</Data>
</Cell>
*/

//token list: column index (%d), style name (%s), value (%llu)
const char* pchCellBlockNumberU64 = "<Cell ss:Index=\"%d\" ss:StyleID=\"%s\">\n	<Data ss:Type=\"Number\">%llu</Data>\n	</Cell>";

/*
<Cell ss:Index="%d" ss:StyleID="%s" ss:MergeAcross="%d">
	<Data ss:Type="String">%s</Data>
</Cell>
*/

//token list: column index (%d), # to merge (%d), style name (%s), value (%s)
const char* pchCellBlockString = "<Cell ss:Index=\"%d\" ss:StyleID=\"%s\" ss:MergeAcross=\"%d\">\n	<Data ss:Type=\"String\">%s</Data>\n	</Cell>";

/*
<Cell ss:Index="%d" ss:StyleID="%s" ss:Formula="%s">
	<Data ss:Type="Number">%d</Data>
</Cell>
*/

//token list: column index (%d), style name (%s), formula (%s), value %d
const char* pchCellBlockNumber = "<Cell ss:Index=\"%d\" ss:StyleID=\"%s\" ss:Formula=\"%s\">\n	<Data ss:Type=\"Number\">%d</Data>\n	</Cell>";

/*
<Cell ss:Index="%d" ss:StyleID="%s" ss:Formula="%s">
	<Data ss:Type="Number">%d</Data>
</Cell>
*/

//token list: column index (%d), style name (%s), value %f
const char* pchCellBlockNumberFloat = "<Cell ss:Index=\"%d\" ss:StyleID=\"%s\">\n	<Data ss:Type=\"Number\">%f</Data>\n	</Cell>";

const char* pchDocumentFooter = "</Workbook>";
