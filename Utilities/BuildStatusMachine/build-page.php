<!--
<meta http-equiv="refresh" content="5">
-->

<?
include_once "www-header.php";

include_once "php/bs_mysql.php";

include_once "php/my-library.php";

$link = bs_mysql_connect(); 

$build_id = get_build_id();

print "<form>";

echo "<h1 align=\"center\">BUILD: </A>";
print "<input type=\"text\" name=\"build\" value=\"$build_id\">";
print "<input type=\"submit\" value=\"Go\"></h1>";
print "</form>";

$sql = "SELECT * FROM `build` WHERE `build`.`build_id` = \"$build_id\" LIMIT 1 ";
$result = mysql_query ( $sql );
if(!$result) {
	die ('Query failed! : ' . mysql_error());
}
$record = mysql_fetch_array($result, MYSQL_BOTH);

// Build Data
display_build_data($record);
print "<br>";

// Show Build E-mail (needs scrollbar)
display_build_email($record);


mysql_close($link);


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
// Build Data
function display_build_data($record) {
	$machine = $record["machine"];
	$machine = get_machine_name_from_id($machine);
	$ucmachine = strtoupper($machine);
	$sFullLink = "<A href=\"machine-page.php?machine=$machine\">$ucmachine</A>";
	$sHTMLtoCBlink = "<A href=\"http://$machine/\">WEB</A>";
	$sVNCtoCBlink = "<A href=\"cryptic://vnc/$machine\">VNC</A>";
	// cryptic://vnc/qa_01
	print "Builder: $sFullLink $sHTMLtoCBlink $sVNCtoCBlink<br>";

	$product_name = $record["product_name"];
	print "Project: $product_name<br>";

	$package_location = $record["package_location"];
	$package_link = $record["package_link"];
	$myHostName = $_SERVER['SERVER_NAME'];
	$sFullLink = "<A href=\"http://$myHostName/$package_link/BuildLogs.zip\">$package_location</A>";
	print "Log Archive: $sFullLink<br>";

	$build_start_time = $record["build_start_time"];
	print "Build start timee: $build_start_time<br>";

	$patch_version = $record["patch_version"];
	$patch_result = $record["patch_push_complete"];
	if(strcmp($patch_result, "FALSE") == 0) {
		$patchmaster_link = "$patch_version";
	} else {
		$patchmaster_link = "<A href=\"http://patchmaster/$product_name/view/$patch_version/\">$patch_version</A>";
	}
	print "Patch Version: $patchmaster_link<br>";

	$gimme_branch_core = $record["gimme_branch_core"];
	print "Gimme Branch Core: $gimme_branch_core<br>";

	$gimme_branch_patch = $record["gimme_branch_patch"];
	print "Gimme Branch Project: $gimme_branch_patch<br>";

	$gimme_branch = $record["gimme_branch"];
	print "Gimme Branch: $gimme_branch<br>";

	$gimme_time = $record["gimme_time"];
	print "Gimme Time: $gimme_time<br>";

	$svn_revision = $record["svn_revision"];
	print "SVN Revision: $svn_revision<br>";

	$svn_branch = $record["svn_branch"];
	print "SVN Branch: $svn_branch<br>";

	$svn_incremental_summary = $svn_incremental_summary["svn_incremental_summary"];
	print "SVN Incremental Summary: $svn_incremental_summary<br>";
}

// Show Build E-mail (needs scrollbar)
function display_build_email($record) {
	$email_body = $record["build_email_body"];
	$email_body = wordwrap($email_body, 120); // number estimated based on my screen resolution
	print "<table border=1 align=\"center\" width=\"95%\"><tr><th>Email Body</th></tr><tr>";
	print "<td style=\"word-break:break-all\"><pre>$email_body</pre></td>";
	print "</tr></table>";
}

function get_build_id() {
	$build_label = $_GET["build_patch"]; // Alternative lookup
	if(!$build_label) $build_label = "";
	// TODO lookup label to get build ID

	if(!$build_id) $build_id = $_GET["build"];
	// no parameter given - get latest build ID
	if(!$build_id) {
		$sql = "SELECT * FROM `build` ORDER BY `build`.`build_id` DESC LIMIT 1 ";
		$result = mysql_query ( $sql );
		if(!$result) {
			die ('Query failed! : ' . mysql_error());
		}
		$row = mysql_fetch_array($result, MYSQL_BOTH);

		$build_id = $row["build_id"];
	}
	return $build_id;
}



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

	print "<table border=\"2\" width=\"100%\"cellpadding=\"2\" cellspacing=\"1\">";
	print "<tr> <th width=\"110px\">Last Update</th> <th>Current Process</th> <th width=\"110px\">Time in Step</th> <th width=\"110px\">Time in Build</th> <th width=\"150px\">Last Result</th> <th>Build Start Time</th> <tr>";

	printf("<tr> <td align=\"RIGHT\" $LastUpdateStyle >%s</td> <td>%s</td> <td align=\"RIGHT\">%s</td> <td align=\"RIGHT\">%s</td> <td>%s</td> <td>%s</td> </tr>", $prettyTime, $sCurrentStatus, $sSecsInCurrentState, $sSecsInCurrentBuild, $sLastBuildResult, $last_build);

	if($sStatusSubSubString != "") {
		print "<tr> <th colspan=\"6\">Script Step Details</th> <tr>";
		print "<tr> <td >$sStatusSubSubString</td></tr>";
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
	$build_link = $row["build_id"];
	printf("<td align=\"CENTER\">%s</td>", $build_link); 
	
	// Success or Fail
	$build_result = "Unavailable";
	printf("<td align=\"CENTER\">%s</td>", $build_result); 

	// Link to Patch Master (by build name PM4_BLAH_BLAH_01)
	$project = $row["product_name"];
	// http://patchmaster/$project/view/FC.9.20090223.3/
	$patch_version = $row["patch_version"]; // "PM5_01_01_01_1i";
	$patchmaster_link = "http://patchmaster/$project/view/$patch_version/";
	// TODO Curl the page, if it's a 404 then don't show as link...
	printf("<td align=\"CENTER\"><A href=\"$patchmaster_link\">$patch_version</A></td>", $patchmaster_link); 

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




include_once "www-footer.php";

?>

