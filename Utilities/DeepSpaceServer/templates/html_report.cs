<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>Downloader Report</title>
<link rel="stylesheet" type="text/css" href="dlermain.css" />
<!--[if IE]>
<link rel="stylesheet" type="text/css" href="dlerie.css" />
<![endif]-->
</head>

<body>

<!-- header -->
<div id="header">

<img id="headerlogo" src="pwrd5.png" width="129" height="79" alt="Perfect World" />

<h1 id="headertitle">
<?cs var:Titletext ?>
</h1>
</div> <!-- end header -->

<div id="wrapper">
<!-- main weekly stats table -->
<div id="mainstats" align="center">
<h2>Daily Download Stats</h2>
<table cols="7" title="Daily Stats">
	<tr>
		<th></th>
        <th>Started</th>
        <th>Finished</th>
        <th>Installs</th>
        <th>Canceled</th>
        <th>Resumed</th>
        <th>%</th>
	</tr>

	<?cs each:day = Days ?>
		<?cs if:day.Row == "1" ?>
			<tr>
		<?cs else ?>
			<tr class="alt">
		<?cs /if ?>
			<td><?cs var:day.Label ?></td>
			<td><?cs var:day.Started ?></td>
			<td><?cs var:day.Finished ?></td>
			<td><?cs var:day.Installs ?></td>
			<td><?cs var:day.Canceled ?></td>
			<td><?cs var:day.Resumed ?></td>
			<td><b><?cs var:day.Completionpercentadjusted ?></b></td>
		</tr>

	<?cs /each ?>

    <tr id="tabletotals"> <!-- TOTALS -->
		<td><?cs var:Days_Total.Label ?></td>
		<td><?cs var:Days_Total.Started ?></td>
		<td><?cs var:Days_Total.Finished ?></td>
		<td><?cs var:Days_Total.Installs ?></td>
		<td><?cs var:Days_Total.Canceled ?></td>
		<td><?cs var:Days_Total.Resumed ?></td>
		<td><b><?cs var:Days_Total.Completionpercentadjusted ?></b></td>
    </tr>

</table>
</div> <!-- end mainstats -->

<div id="tables">
<table cols="6" title="Weekly Stats by Country">
	<tr>
		<th></th>
        <th>Started</th>
        <th>Finished</th>
        <th>Installs</th>
        <th>Canceled</th>
        <th>Resumed</th>
	</tr>

	<?cs each:country = Countries ?>
		<?cs if:country.Label == "Total" ?>
			<tr id="tabletotals">
		<?cs elif:country.Row == "1" ?>
			<tr>
		<?cs else ?>
			<tr class="alt">
		<?cs /if ?>

			<td><?cs var:country.Label ?></td>
			<td><?cs var:country.Started ?></td>
			<td><?cs var:country.Finished ?></td>
			<td><?cs var:country.Installs ?></td>
			<td><?cs var:country.Canceled ?></td>
			<td><?cs var:country.Resumed ?></td>

		</tr>
	<?cs /each ?>
 </table>

<table cols="6" title="Weekly Stats by Locale">
	<tr>
		<th></th>
        <th>Started</th>
        <th>Finished</th>
        <th>Installs</th>
        <th>Canceled</th>
        <th>Resumed</th>
	</tr>

	<?cs each:locale = Locales ?>
		<?cs if:locale.Label == "Total" ?>
			<tr id="tabletotals">
		<?cs elif:locale.Row == "1" ?>
			<tr>
		<?cs else ?>
			<tr class="alt">
		<?cs /if ?>

			<td><?cs var:locale.Label ?></td>
			<td><?cs var:locale.Started ?></td>
			<td><?cs var:locale.Finished ?></td>
			<td><?cs var:locale.Installs ?></td>
			<td><?cs var:locale.Canceled ?></td>
			<td><?cs var:locale.Resumed ?></td>

		</tr>
	<?cs /each ?>
 </table>
    
<table cols="4" title="Download Efficiency">
	<tr>
		<th></th>
        <th>GB DC</th>
        <th>GB Peers</th>
        <th>Efficiency</th>
	</tr>

	<?cs each:day = Days ?>
		<?cs if:day.Row == "1" ?>
			<tr>
		<?cs else ?>
			<tr class="alt">
		<?cs /if ?>
			<td><?cs var:day.Label ?></td>
			<td><?cs var:day.Gbcryptic ?></td>
			<td><?cs var:day.Gbpeers ?></td>
			<td><?cs var:day.Efficiency ?>%</td>
		</tr>

	<?cs /each ?>

    <tr id="tabletotals"> <!-- TOTALS -->
		<td><?cs var:Days_Total.Label ?></td>
		<td><?cs var:Days_Total.Gbcryptic ?></td>
		<td><?cs var:Days_Total.Gbpeers ?></td>
		<td><?cs var:Days_Total.Efficiency ?>%</td>
    </tr>

</table>

</div> <!-- end tables -->

<div id="graphs">
<!-- PLACEHOLDER, ACTUAL GRAPHS GO HERE -->
<img src="<?cs var:Filename_Downloadsperhour ?>" width="485" height="150" alt="Weekly Started Downloads Per Hour" /><br />
<img src="<?cs var:Filename_Downloaddurationhist ?>" width="485" height="150" alt="Completed Downloads Duration Histogram, in Minutes" /><br />
<img src="<?cs var:Filename_Dropout ?>" width="485" height="150" alt="Dropout Percent" /><br />
<img src="<?cs var:Filename_Downloadrate ?>" width="485" height="150" alt="Download Rate" /><br />
</div> <!-- end graphs -->

<div id="toc">
<!-- TABLE OF CONTENTS? -->
</div>

</div> <!-- end wrapper -->
</body>
</html>
