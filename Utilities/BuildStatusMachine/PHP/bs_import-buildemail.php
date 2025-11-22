<?php

include_once "bs_mysql.php";
include_once "bs_unzip.php";

// Main entry function.
function bs_import_buildEmail( $buildID, $sPackageLocation ) {
	$link = bs_mysql_connect(); 

	// find which email file it is\
	$data = bs_find_email_file_and_get_contents( $sPackageLocation );
	if($data) {
		// import email file contents into SQL
		bs_write_mysql_buildEmail($buildID, $data);
//		echo "Email imported for build $buildID \n";
	}
	else {
//		echo "No email found for build $buildID \n";
	}

	mysql_close($link);
}

function bs_find_email_file_and_get_contents( $directory ) {
	// different kinds of builds have different email file names
	// continuous builders have CBAllClearEmail.txt, CBCheckinFailedEmail.tx, BreakageEmail.txt
	// production builders have ...
	// performance builders have ...

	$foundfile = NULL;

	$filename = $directory . "\\CBAllClearEmail.txt";
	$bResult = file_exists($filename);
	if($bResult == TRUE) {
		$foundfile = $filename; 
	}

	// checkin failed email after allclear email as both can exist
	$filename = $directory . "\\CBCheckinFailedEmail.txt";
	$bResult = file_exists($filename);
	if($bResult == TRUE) {
		$foundfile = $filename; 
	}

	$filename = $directory . "\\BreakageEmail.txt";
	$bResult = file_exists($filename);
	if($bResult == TRUE) {
		$foundfile = $filename; 
	}

	$filename = $directory . "\\autoEmail.txt";
	$bResult = file_exists($filename);
	if($bResult == TRUE) {
		$foundfile = $filename; 
	}

   // open the file and get the contents
	$data = NULL;
	if($foundfile != NULL) {
		$fh = fopen($foundfile, "r");
		$data = fread($fh, filesize($foundfile));
		fclose($fh);
	}

   // return the contents
   return $data;
}

function bs_write_mysql_buildEmail( $buildID, $data ) {
	$data = mysql_real_escape_string($data);
	$sql = "UPDATE `buildstatus`.`build` SET `build_email_body` = \"$data \" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { 
		echo "Query failed! : " . mysql_error() . "\n";
	}
}




?> 
