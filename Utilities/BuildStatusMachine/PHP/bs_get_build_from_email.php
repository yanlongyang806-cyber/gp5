<?php

include_once "bs_unzip.php";
include_once "bs_mysql.php";
include_once "bs_import-build.php";

//get_all_builds_from_email();

function get_all_builds_from_email() {

	// How many emails are waiting?
	$count = bs_get_email_count();
	if($count < 1) {
		echo "No messages found. \n";
		return FALSE;
	}

	// DEBUG
//	$count = 2;

	for($i = 0; $i < $count; $i++) {
		$bResult = get_next_build_from_email();
	}
}


function get_next_build_from_email() {
	// Get the e-mail attachment
	$attachmentdir='C:\\BuildStatus\\temp';
	$gotemail = bs_get_next_attachment( $attachmentdir );
	if($gotemail == FALSE) {
		echo "Did not get any e-mails. \n";
		return FALSE;
	}
	// Get a new build ID
	$buildID = bs_get_new_buildID();

	// Extract the build to a temp location 
	$zipfile = 'C:\\BuildStatus\\temp\\BuildLogs.zip';
	$extract_dir = 'C:\\BuildStatus\\temp\\BuildLogs\\';
	system("rmdir /S /Q $extract_dir");
	bs_unzip( $zipfile, $extract_dir );

	// Run the import build script (give it buildID, location)
	bs_import_build($buildID, $extract_dir);

	// Findout where it expects the stuff to be (determied during import variables within bs_import_build)
	$link = bs_mysql_connect(); 
	$sql = "SELECT `package_location` FROM `build` WHERE `build`.`build_id` = $buildID LIMIT 1 ";
	$result = mysql_query ( $sql );
	if(!$result) { die ('Query failed! : ' . mysql_error()); }
	$row = mysql_fetch_object($result);
	$sLocation = $row->package_location;
	mysql_free_result($result);
	mysql_close($link);

	// Now move it there
	@mkdir($sLocation, 0777, TRUE);
	$cmd = "move /Y $zipfile $sLocation 2>&1";
	$output = system($cmd);

	// echo "Clean up the temp directory";
	system("rmdir /S /Q $extract_dir");
	echo "Build $buildID imported to $sLocation \n";
	return TRUE;
}

function bs_open_mailbox()
{
	$server = "{proserpine:993/imap/ssl}INBOX";
	$login = "BuildStatusMachine";
	$password = "none123";

	// Dumping any previous errors
	$foo = imap_errors();

	@mkdir($destination_path, 0777, TRUE);

	$mailbox = imap_open("$server", "$login", "$password");
	if ($mailbox == false) {
		echo "Call failed \n";
		$error = imap_last_error();
		echo "Error: $error \n";
		return FALSE;
	}
	return $mailbox;
}

function bs_get_next_attachment( $destination_path ) {


	@mkdir($destination_path, 0777, TRUE);

	$mailbox = bs_open_mailbox();

	// We only scrape the 1st messages attachment
	$msgno = 1;

	$struct = imap_fetchstructure($mailbox,$msgno);
	if($struct == false) {
		 echo "Call failed \n";
		$error = imap_last_error();
		echo "Error: $error \n";
		return FALSE;
	}

	$contentParts = count($struct->parts);
	if($contentParts == false) {
		echo "Call failed \n";
		$error = imap_last_error();
		echo "Error:  $error, \n";
		return FALSE;
	}
	if ($contentParts < 2) {
		echo "No attachment detected!  \n";
		return FALSE;
	}
   
	// Verify it's a ZIP file
	if ($contentParts >= 2) {
		$att = imap_bodystruct($mailbox,$msgno,2);
	}

	$strFileName = $att->parameters[0]->value;

	$strFileType = strrev(substr(strrev($strFileName),0,4));
	if($strFileType != '.zip')
	{
		echo "Attachment was not a zip file! \n";
		return FALSE;
	}
	// Yep, it's a ZIP

	$fileContent = imap_fetchbody($mailbox,$msgno,2);

	$binaryFileContent = base64_decode ( $fileContent );

	$fileNameWithPath = "$destination_path\\$strFileName";

	// If a old zip is there, NUKE IT *BOOM*
	@unlink($fileNameWithPath);

	$fileHandle = fopen ( $fileNameWithPath , "x" );
	if($fileHandle == FALSE) {
		echo "Failed to open file $fileNameWithPath \n ";
		return FALSE;
	}
	$writecount = fwrite ( $fileHandle , $binaryFileContent );
	$result = fclose($fileHandle);
	if($result == FALSE) {
		echo "Something screwy, failed to close file $fineNameWithPath \n";
	}

	// DEBUG
//	echo "DEBUG Exiting before deleting original mail \n";
//	imap_close($mailbox);
//	return TRUE;

	$bResult = imap_delete ( $mailbox , $msgno );
	if ($bResult == false) {
		echo "Failed to flag message for deletion!  \n";
		return FALSE;
	}
	$bResult = imap_expunge ( $mailbox );
	if ($bResult == false) {
		echo "Failed to delete message!  \n";
		return FALSE;
	}
	imap_close($mailbox);
	return TRUE;
}

function bs_get_email_count() {

	$mailbox = bs_open_mailbox();
	// How many emails are waiting?
	$count = imap_num_msg($mailbox);
	imap_close($mailbox);
	return $count;

}
?> 
