<?php
include_once "C:/BuildStatus/PHP/bs_mysql.php";

function UpdateCurrentStatusFromBuilder($ID, $name) {
	$sXMLstring = GetStatusXMLFromBuilder($name);
	if($sXMLstring == FALSE)
	{
		echo "No response from $name \n";
		unset($sXMLstring);
		return FALSE;
	}
	echo "XML recieved from $name \n";

	$xml = simplexml_load_string($sXMLstring);

	//CURSTATESTRING
	$currentstate = mysql_real_escape_string($xml->curstatestring);
	$sql = "UPDATE `buildstatus`.`machine` SET `current_status` = \"$currentstate\" WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { echo 'Query failed! : ' . mysql_error(); return FALSE; }
	unset($currentstate);
	unset($sql);
	unset($bResult);

	//CURSTATETIME
	$currentstatetime = $xml->currentstatetime;
	$sql = "UPDATE `buildstatus`.`machine` SET `time_in_current_state` = \"$currentstatetime\" WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($currentstatetime);
	unset($sql);
	unset($bResult);

	//CURSTATETIME SECONDS
	$secondsincurrentstate = $xml->secondsincurrentstate;
	$sql = "UPDATE `buildstatus`.`machine` SET `seconds_in_current_state` = \"$secondsincurrentstate\" WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($secondsincurrentstate);
	unset($sql);
	unset($bResult);

	//TIMEINBUILD
	$timeinbuild = $xml->timeinbuild;
	$sql = "UPDATE `buildstatus`.`machine` SET `time_in_current_build` = \"$timeinbuild\" WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($timeinbuild);
	unset($sql);
	unset($bResult);

	//TIMEINBUILD SECONDS
	$secondsinbuild = $xml->secondsinbuild;
	$sql = "UPDATE `buildstatus`.`machine` SET `seconds_in_current_build` = \"$secondsinbuild\" WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($secondsinbuild);
	unset($sql);
	unset($bResult);

	//STATUSSUBSTRING
	$statussubstring = mysql_real_escape_string($xml->statussubstring);
	$sql = "UPDATE `buildstatus`.`machine` SET `status_sub_string` = '$statussubstring' WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($statussubstring);
	unset($sql);
	unset($bResult);

	//STATUSSUBSUBSTRING
	$statussubsubstring = mysql_real_escape_string($xml->statussubsubstring);
	$sql = "UPDATE `buildstatus`.`machine` SET `status_sub_sub_string` = '$statussubsubstring' WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($statussubsubstring);
	unset($sql);
	unset($bResult);

	//LASTBUILDRESULT
	$lastbuildresult = $xml->lastbuildresult;
	$sql = "UPDATE `buildstatus`.`machine` SET `last_build_result` = '$lastbuildresult' WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($lastbuildresult);
	unset($sql);
	unset($bResult);

	//LASTSUCCESSFULLBUILDTIME
	$lastsuccessfullbuildtime = $xml->lastsuccessfullbuildtime;
	$sql = "UPDATE `buildstatus`.`machine` SET `last_successfull_build_time` = '$lastsuccessfullbuildtime' WHERE `machine`.`machine_id` = $ID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	unset($lastsuccessfullbuildtime);
	unset($sql);
	unset($bResult);

	return TRUE;
}

function GetStatusXMLFromBuilder($hostname) {
	$ch = curl_init(); 
	$url = "http://$hostname/statusXML";
	curl_setopt($ch, CURLOPT_URL, $url); 
	unset($url);

	// binary data result
	curl_setopt($ch, CURLOPT_TRANSFERTEXT, TRUE); // ASCII mode 
//	curl_setopt($ch, CURLOPT_BINARYTRANSFER, TRUE);

	// force fresh connection
	curl_setopt($ch, CURLOPT_FRESH_CONNECT, TRUE);

	// Don't wait long for timeout (seconds)
	curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);

	// Don't wait long for timeout (seconds)
	curl_setopt($ch, CURLOPT_TIMEOUT, 2);

	//return the transfer as a string 
	curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1); 
	// $output contains the output string 
	$output = curl_exec($ch); 
	if($output == FALSE) {
		unset($output);
		return FALSE;
	}
//	echo "Output is $output \n";
	$imatch = strncmp($output, '<html>', 6);
	if($imatch == 0) {
		// we got a default page, not the status XML page
		unset($output);
		unset($imatch);
		curl_close($ch);
		unset($ch);
		return FALSE;
	}
	unset($imatch);

	curl_close($ch);
	unset($ch);
	return $output;
}

function UpdateCurrentStatusFromAllBuilders() {
	$link = bs_mysql_connect(); 

	// Attempt to update all machine statuses
	$sql = "SELECT * FROM `machine`";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query failed! : ' . mysql_error());
	}
	unset($sql);

	while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
		$smachineID = $row["machine_id"];
		$smachinename = $row["host_name"];
		$bmachineup = UpdateCurrentStatusFromBuilder($smachineID, $smachinename);
		unset($bmachineup);
		unset($smachineID);
		unset($smachinename);
	}
	mysql_free_result($result);
	mysql_close($link);
	unset($row);
}

?>

