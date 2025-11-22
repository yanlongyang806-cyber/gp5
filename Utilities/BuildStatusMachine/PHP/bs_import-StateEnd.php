<?php

include_once "bs_mysql.php";

function bs_import_StateEnd( $buildID, $logFileLocation ) {
	// Get the variables
	$link = bs_mysql_connect(); 

	$bResult = file_exists($logFileLocation);
	if($bResult == FALSE) {
		echo "File not found: $logFileLocation! \n";
		return FALSE;
	}

	$bResult = file_exists($logFileLocation);
	if($bResult == FALSE) {
		echo "File not found: $logFileLocation! \n";
		return FALSE;
	}
	$data = file_get_contents ( $logFileLocation, FILE_TEXT );
	if( $data == false) {
		echo "Couldn't read from file $logFileLocation \n";
		return FALSE;
	}
	$xml = simplexml_load_string($data);

	//CURSTATESTRING
	$currentstate = mysql_real_escape_string($xml->currentresult);

//	print "FINAL STATE = $currentstate \n";
	$sql = "UPDATE `buildstatus`.`build` SET `build_end_state` = \"$currentstate\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { echo 'Query failed! : ' . mysql_error(); return FALSE; }

	mysql_close($link);
}
?> 
