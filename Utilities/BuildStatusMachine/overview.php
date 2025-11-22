<meta http-equiv="refresh" content="2">

<?
include_once "www-header.php";

include_once "php/my-library.php";

print "[<a href=\"overview.php?filter=project\">Filter By Project</a>]";
print "[<a href=\"overview.php?filter=type\">Filter By Type</a>]";
print "[<a href=\"show-machines.php\">Edit List</a>]<br>";

include_once "php/bs_mysql.php";

$all_machines = get_array_of_all_machines();

$filter = $_GET["filter"];
if(!$filter) $filter = "project";

// Should be in a DB table
$prettyNames = array(
	'QA' => 'Performance Tester',
	'CONT' => 'Continuous Builder',
	'PROD' => 'Production Builder',
	'INCR' => 'Incremental Builder',
	'PERF' => 'Performance Tester',
	'TEST' => 'Miscelaneous'
);

$categories = array();

foreach($all_machines as $machine)
{
	$cat = get_category($filter, $machine);

	if(!$categories[$cat])
		$categories[$cat] = array();
	array_push($categories[$cat], $machine);
}

print "<table border=\"2\" width=\"100%\" cellspacing=\"0\"";
print "<tr><th width=\"200px\">Machine</th> <th width=\"110px\">Last Update</th> <th>Current Process</th> <th width=\"110px\">Time in Step</th> <th width=\"110px\">Time in Build</th> <th width=\"150px\">Last Result</th> <th>Start Time</th> <tr>";

// sort($categories);

foreach($categories as $cat => $val)
{
	$pretty_category_name = find_pretty_category_name($cat);
	$pretty_category_name = strtoupper($pretty_category_name);
	print "<tr> <th colspan=\"7\"><font size=\"4\"> $pretty_category_name</font></td> </tr>";

	foreach($categories[$cat] as $machine)
	{
		display_machine_table($machine);
	}
//	print "<tr> <td colspan=\"7\"> <br> </td> </tr>"; // blank space
}
print "</table><br>";




////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
function display_machine_table($machine) {
	// Get the machine record from the DB
	$link = bs_mysql_connect(); 
	// Display the results from DB
	$sql = "SELECT *,UNIX_TIMESTAMP(`last_update`) FROM `machine` WHERE `machine`.`host_name` = \"$machine\" LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$row = mysql_fetch_array($result, MYSQL_BOTH);

	$showMachine = $row["show_machine"];
	if($showMachine != "1") { 
		mysql_free_result($result);
		mysql_close($link);
		RETURN; 
	}
	$sHostName = $row["host_name"];

//	$sHostIP = $row["host_ip"];

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
//	$sStatusSubSubString = $row["status_sub_sub_string"];
//	if(!$sStatusSubSubString) {
//		$sStatusSubSubString = "";
//	}

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

	$last_build = get_last_build_from_machine($sHostName);
	
	// Color code the last update field for significant attention.
	if($sTimeSince > 60) {$LastUpdateStyle = "class=\"failbackground\""; }
	if($prettyTime == "OFFLINE") { $LastUpdateStyle = "class=\"warnbackground\""; }

	$sHTMLtoCBlink = "<A href=\"http://$sHostName/\">WEB</A>";
	$sVNCtoCBlink = "<A href=\"cryptic://vnc/$sHostName\">VNC</A>";

	print "<tr><td align=\"CENTER\">$sFullLink $sHTMLtoCBlink $sVNCtoCBlink</td>";
	print "<td align=\"RIGHT\" $LastUpdateStyle >$prettyTime</td>";
	print "<td><div style=\"overflow:hidden; height:20px\">$sCurrentStatus</div></td>";
	print "<td align=\"RIGHT\">$sSecsInCurrentState</td>";
	print "<td align=\"RIGHT\">$sSecsInCurrentBuild</td>";
	print "<td>$sLastBuildResult</td>";
	print "<td>$last_build</td></tr>";

	mysql_free_result($result);
	mysql_close($link);
}



function get_array_of_all_machines() {
	// return an array of all machines
	$link = bs_mysql_connect();

	$sql = "SELECT * FROM `machine` LIMIT 0, 30 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}

	$all_machines = array();
	while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
		$sHostName = $row["host_name"];
		$all_machines[] = $sHostName;
	}

	mysql_free_result($result);
	mysql_close($link);
	return $all_machines;
}

function get_category($filter, $machine)
{
	$matches = array();

	if(preg_match("/cb-(\S+)-([a-z]+)(\d+)/i", $machine, $matches))
	{
		$project = strtoupper($matches[1]);
		$type    = strtoupper($matches[2]);
		$number  = $matches[3];

		if($filter == 'project')
		{
			// By Project
			if($project == "CO") { // Both mean FightClub
				$project = "FC";
			}
			return $project;
		}
		else
		{
			// By Type
			return $type;
		}
	}

	return "Unknown";
}

function find_pretty_category_name($cat)
{	
	$prettyName = get_public_name_from_short($cat);
//	$prettyName = get_internal_name_from_short($cat);
	if(!$prettyName) {
		global $prettyNames;
		$prettyName = $prettyNames[$cat];
	}
	if($prettyName)
		return $prettyName;

	return $cat;
}





?>

