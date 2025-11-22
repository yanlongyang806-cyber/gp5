<?php

function bs_get_new_buildID() {


	// get new build ID from the database
	$link = bs_mysql_connect();

	$sql = "INSERT INTO `buildstatus`.`build` ( `build_id` ) VALUES ( NULL )";
	$bResult = mysql_query ( $sql );
	if(!$bResult) {
	    die ('Query failed! : ' . mysql_error());
	}
	// Get the machine ID back out....
	$build_id = mysql_insert_id($link);

	mysql_close($link);

	return $build_id;
}

function bs_mysql_connect() {
	$link = mysql_connect('localhost', 'root', '');
	if (!$link) {
	    die('Could not connect: ' . mysql_error());
	}

	$db_selected = mysql_select_db('BuildStatus', $link);
	if (!$db_selected) {
	    die ('Can\'t use BuildStatus DB : ' . mysql_error());
	}

	return $link;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Some common/popular calls
///////////////////////////////////////////////////////////////////////////////////////////////////////////
function get_machine_id( $host_name, $host_ip ) {
	$link = bs_mysql_connect();

	$sql = "SELECT * FROM `machine` LIMIT 0, 30 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}

	$machine_id = false;
	while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
		$sHostName = $row["host_name"];
		$sHostIP = $row["host_ip"];
		if(($host_name == $sHostName) && ($host_ip == $sHostIP)) {
//			echo "We found a match! Ding ding ding! \n";
			$machine_id = $row["machine_id"];
			break;
		}
	}

	mysql_free_result($result);

	if ($machine_id == false) {
		// Add new machine
		$sql = "INSERT INTO `buildstatus`.`machine` ( `machine_id` , `host_name` , `host_ip` ) VALUES ( NULL , \"$host_name\", \"$host_ip\" )";
		$bResult = mysql_query ( $sql );
		if(!$bResult) {
		    die ('Query failed! : ' . mysql_error());
		}
		// Get the machine ID back out....
		$machine_id = mysql_insert_id($link);
	}

	mysql_close($link);
	return $machine_id;
}

// This function assumes a link is active and valid.
function get_machine_name_from_id( $machine_id ) {
	$sql = "SELECT `host_name` FROM `machine` WHERE `machine_id` = $machine_id LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$sResult = mysql_result($result, 0);

	mysql_free_result($result);
	return $sResult;
}

function get_machine_id_from_hostname( $hostname ) {
	$sql = "SELECT `machine_id` FROM `machine` WHERE `host_name` = \"$hostname\" LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$sResult = mysql_result($result, 0);

	mysql_free_result($result);
	return $sResult;
}

// Fairly commonly used function by other scripts
function get_build_package_location( $buildID ) {
	$sql = "SELECT `package_location` FROM `build` WHERE `build_id` = $buildID LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$sResult = mysql_result($result, 0);
	$sResult = trim($sResult); // Some data has extra space on end... (this is bad)
	mysql_free_result($result);
	return $sResult;
}

function get_last_build_from_machine($hostname) {
	$machine_id = get_machine_id_from_hostname($hostname);

	// Just get the last build that matches
	$sql = "SELECT * FROM `build` WHERE `machine` = \"$machine_id\" ORDER BY `build`.`build_id` DESC LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}

	while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
		$buildstarttime = $row["build_start_time"];
	}

	mysql_free_result($result);
	return $buildstarttime;
}

function get_internal_name_from_short($shortname) {
	$link = bs_mysql_connect();

	$lcname = strtolower($shortname);
	$sql = "SELECT * FROM `project_abbreviations` WHERE `project_abbreviation` = \"$lcname\" LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$row = mysql_fetch_array($result, MYSQL_BOTH);
	$iProjectIndex = $row["project_id"];
	mysql_free_result($result);

	$sInternalName = NULL;
	if($iProjectIndex) {
		$sql = "SELECT * FROM `project_names` WHERE `project_id` = $iProjectIndex LIMIT 1";
		$result = mysql_query ( $sql );
		if(!$result) {
			die ('Query failed! : ' . mysql_error());
		}
		$row = mysql_fetch_array($result, MYSQL_BOTH);
		$sInternalName = $row["internal_name"];
		mysql_free_result($result);
	}
	mysql_close($link);
	return $sInternalName;
}

function get_public_name_from_short($shortname) {
	$link = bs_mysql_connect();

	$lcname = strtolower($shortname);
	$sql = "SELECT * FROM `project_abbreviations` WHERE `project_abbreviation` = \"$lcname\" LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	$row = mysql_fetch_array($result, MYSQL_BOTH);
	$iProjectIndex = $row["project_id"];
	mysql_free_result($result);

	$sInternalName = NULL;
	if($iProjectIndex) {
		$sql = "SELECT * FROM `project_names` WHERE `project_id` = $iProjectIndex LIMIT 1";
		$result = mysql_query ( $sql );
		if(!$result) {
			die ('Query failed! : ' . mysql_error());
		}
		$row = mysql_fetch_array($result, MYSQL_BOTH);
		$sInternalName = $row["public_name"];
		mysql_free_result($result);
	}
	mysql_close($link);
	return $sInternalName;
}



?> 
