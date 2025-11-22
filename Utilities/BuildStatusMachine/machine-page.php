<!--
<meta http-equiv="refresh" content="5">
-->

<?
include_once "www-header.php";

include_once "php/bs_mysql.php";

include_once "php/my-library.php";

$link = bs_mysql_connect(); 

$machine = $_GET["machine"];
if(!$machine) $machine = "cb-co-cont1";

// Create a machine menu
write_machine_menu($machine);

$uc_machine = strtoupper($machine);
$sVNCtoCBlink = "<A href=\"cryptic://vnc/$machine\">VNC</A>";

echo "<h1 align=\"center\">BUILDER: $uc_machine <A href=\"http://$machine/\">WEB</A> $sVNCtoCBlink</h1>";

// Display Current Status
display_current_status($machine);

// Build History
echo "<h2 align=\"Center\">Build History</h2>";
//print "<table border=\"2\" width=\"100%\"cellpadding=\"2\" cellspacing=\"1\">";
print "<table width=\"100%\" border=\"2\" cellspacing=\"0\">";
print "<tr>";
print "<th>Build</th>";
print "<th>Result</th>";
print "<th>Patch Master</th>";
print "<th>SVN Rev</th>";
print "<th>Gimme Time</th>";
print "<th>Core Gimme Branch</th>";
print "<th>Proj Gimme Branch</th>";
//print "<th>SVN Branch</th>";
print "<tr>";

// For each build on this machine, sort by build ID DESCENDING
$machine_id = get_machine_id_from_hostname($machine);
$sql = "SELECT * FROM `build` WHERE `build`.`machine` = \"$machine_id\" ORDER BY `build`.`build_id` DESC";
$result = mysql_query ( $sql );
if(!$result) {
	die ('Query failed! : ' . mysql_error());
}
while($row = mysql_fetch_array($result, MYSQL_BOTH)) {
	$build_id = $row["build_id"];
	display_build_history($build_id);
}

mysql_close($link);


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
function display_current_status($machine) {
	echo "<h2 align=\"Center\">Current Build Status</h2>";

	// Get the machine record from the DB
	// Display the results from DB
	$sql = "SELECT *,UNIX_TIMESTAMP(`last_update`) FROM `machine` WHERE `machine`.`host_name` = \"$machine\" LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$row = mysql_fetch_array($result, MYSQL_BOTH);

	$sTimeStampExists = $row["last_update"];
	if($sTimeStampExists != NULL) {
		$sTimestamp = $row["UNIX_TIMESTAMP(`last_update`)"] ;
		$sTimeSince = time() - $sTimestamp;
		$prettyTime = prettyformat_time($sTimeSince);
	}
	else {
		$sTimestamp = -1;
		$prettyTime = "OFFLINE";
	}
	$sCurrentStatus = $row["current_status"];
	if(!$sCurrentStatus) {
		$sCurrentStatus = "unknown";
	}
	$sStatusSubString = $row["status_sub_string"];
	if(!$sStatusSubString) {
		$sStatusSubString = "";
	}
	$sStatusSubSubString = $row["status_sub_sub_string"];
	if(!$sStatusSubSubString) {
		$sStatusSubSubString = "";
	}

	// This is seconds since it was reported, so we have to add the time since reporting
	$sSecsInCurrentState = $row["seconds_in_current_state"];
	$sSecsInCurrentState += $sTimeSince;
	$sSecsInCurrentState = prettyformat_time($sSecsInCurrentState);

	$sSecsInCurrentBuild = $row["seconds_in_current_build"];
	$sSecsInCurrentBuild += $sTimeSince;
	$sSecsInCurrentBuild = prettyformat_time($sSecsInCurrentBuild);

	$sLastBuildResult = $row["last_build_result"];
	if(!$sLastBuildResult) {
		$sLastBuildResult = "none";
	}
	// TODO Findout what the last build was for this builder and get it's result link and add to link to sLastBuildResult

//	$sLastSuccessfullBuildTime = $row["last_successfull_build_time"];
//	if(!$sLastSuccessfullBuildTime) {
//		$sLastSuccessfullBuildTime = "none";
//	}

	$sFullLink = "<A href=\"machine-page.php?machine=$sHostName\">$sHostName</A>";

	// CB's testing step is all of scripting so lets get more detail
	if($sCurrentStatus == "TESTING") {
		$sCurrentStatus = $sStatusSubString;
	}

	$last_build = get_last_build_from_machine($machine);
	
	// Color code the last update field for significant attention.
	if($sTimeSince > 60) {$LastUpdateStyle = "class=\"failbackground\""; }
	if($prettyTime == "OFFLINE") { $LastUpdateStyle = "class=\"warnbackground\""; }

	print "<table border=\"2\" width=\"100%\" cellspacing=\"0\">";
	print "<tr> <th width=\"110px\">Last Update</th> <th>Current Process</th> <th width=\"110px\">Time in Step</th> <th width=\"110px\">Time in Build</th> <th width=\"150px\">Last Result</th> <th>Build Start Time</th> <tr>";

	printf("<tr> <td align=\"RIGHT\" $LastUpdateStyle >%s</td> <td>%s</td> <td align=\"RIGHT\">%s</td> <td align=\"RIGHT\">%s</td> <td>%s</td> <td>%s</td> </tr>", $prettyTime, $sCurrentStatus, $sSecsInCurrentState, $sSecsInCurrentBuild, $sLastBuildResult, $last_build);

	if($sStatusSubSubString != "") {
		print "<tr> <th colspan=\"6\">Script Step Details</th> <tr>";
		print "<tr> <td colspan=\"6\">$sStatusSubSubString</td></tr>";
	}

	print "</table><br>";
	mysql_free_result($result);

}

function display_build_history($buildID) {
	// Display the results from DB
	$sql = "SELECT * FROM `build` WHERE `build`.`build_id` = \"$buildID\" LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$row = mysql_fetch_array($result, MYSQL_BOTH);
	
	printf("<tr>"); // Creating table row

	// Build start time as Link to the build specific page for this build
	// alternatively if we have the build label, use that instead of the start time (construct the build label dynamically?)
//	$buildID = $row["build_id"];
	$build_link = "<A href=\"build-page.php?build=$buildID\">$buildID</A>";
	printf("<td align=\"CENTER\">%s</td>", $build_link); 
	
	// Success or Fail
	$build_result = $row["build_end_state"];
	if($build_result == NULL) {$build_result = ""; }
	if(strcmp($build_result,"NONE") == 0) {$build_result = "FAILED"; }

	printf("<td align=\"CENTER\">%s</td>", $build_result); 

	// Link to Patch Master (by build name PM4_BLAH_BLAH_01)
	$project = $row["product_name"];
	// http://patchmaster/$project/view/FC.9.20090223.3/
	$patch_version = $row["patch_version"]; // "PM5_01_01_01_1i";
	$patch_result = $row["patch_push_complete"];
	if(strcmp($patch_result, "FALSE") == 0) {
		$patchmaster_link = "$patch_version";
	} else {
		$patchmaster_link = "<A href=\"http://patchmaster/$project/view/$patch_version/\">$patch_version</A>";
	}
	printf("<td align=\"CENTER\">$patchmaster_link</td>", $patchmaster_link); 

	// SVN
	$svn_rev = $row["svn_revision"];
	$svnlink = "<A href=\"http://code:8083/log/?action=stop_on_copy&mode=stop_on_copy&rev=$svn_rev\">$svn_rev</A>";
	printf("<td align=\"CENTER\">%s</td>", $svnlink); 

	// Gimme TIme
	$gimme_time = $row["gimme_time"];
	printf("<td align=\"CENTER\">%s</td>", $gimme_time); 

	// Core Branch
	$core_branch = $row["gimme_branch_core"];
	printf("<td align=\"CENTER\">%s</td>", $core_branch); 

	// Project Branch
	$project_branch = $row["gimme_branch_patch"];
	printf("<td align=\"CENTER\">%s</td>", $project_branch); 

	// SVN Branch
//	$svn_branch = "SVN BRANCH";
//	printf("<td align=\"CENTER\">%s</td>", $svn_branch); 

	printf("</tr>"); // Ending table row

}

function write_machine_menu($current_machine) {
	$sql = "SELECT * FROM `machine` ORDER BY `machine`.`host_name` ASC ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	print "<table border=\"2\" cellspacing=\"0\" width=\"100%\"> <tr><td align=\"CENTER\">";
	print "<table> <tr>";
	while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
		$sHostName = $row["host_name"];
		$showMachine = $row["show_machine"];
		if($showMachine == "1") { 
			if(strcmp(strtoupper($sHostName), strtoupper($current_machine)) == 0) {
				$sHostName = $sHostName;
				print "<td class=\"selectbackground\" ><a href=\"machine-page.php?machine=$sHostName\"><B>$sHostName</B></a></td>";
			}
			else {
				print "<td><a href=\"machine-page.php?machine=$sHostName\">$sHostName</a></td>";
			}
		}
	}
	print "</tr> </table>";
	print "</td></tr> </table>";
}




include_once "www-footer.php";

?>

