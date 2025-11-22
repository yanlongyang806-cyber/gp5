<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" >
<style type="text/css">
<!--
a {
	color:"purple";
}
.passtext {
	color:#00FF00;
}
.passbackground {
	background-color:#00FF00;
	color:#000000;
}
.warntext {
	color:#FFFF00;
}
.warnbackground {
	background-color:#FFFF00;
	color:#000000;
}
.failtext {
	color:#FF0000;
}
.failbackground {
	background-color:#FF0000;
	color:#000000;
}
.selectbackground {
	background-color:#FFFF00;
	color:#000000;
}

-->
</style>

<!-- <font color=\"#00FFFF\">TEAL with tears</font><br> -->

<head>
<meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1" />
<title>Build Status Machine</title>
<img src="gear.png" style="float:left">
<?php 
$machine = strtoupper($_SERVER['SERVER_NAME']);
print "<h1><font color=\"#666699\" size=\"8\">Build Status Machine</font></h1>";
// TODO - add menu items here
?>
<table width=100% border=0px><tr>
<td><table width=100% border=2px cellspacing=0px><tr><th><a href="overview.php?filter=project">Builders Overview</a></th></tr></table></td>
<td><table width=100% border=2px cellspacing=0px><tr><th><a href="machine-page.php?machine=cb-fc-prod1">Builder History</a></th></tr></table></td>
<td><table width=100% border=2px cellspacing=0px><tr><th><a href="build-page.php">Build Details</a></th></tr></table></td>
<td><table width=100% border=2px cellspacing=0px><tr><th><a href="query-page.php">Search</a></th></tr></table></td>
<td><table width=100% border=2px cellspacing=0px><tr><th><a href="phpmyadmin">PHP MyAdmin</a></th></tr></table></td>
<td width=30%></td>
<?php
$sVNCtoBMlink = "<A href=\"cryptic://vnc/$machine\">VNC</A>";
print "<td><p style=\"text-align:right; padding-right: 40px;\" >Hosted on $machine $sVNCtoBMlink<br> </p></td>";
?>
</tr></table>
</td></tr></table>
</head>

<!--
<body background="background.jpg">
-->