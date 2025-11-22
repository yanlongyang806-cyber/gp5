<?php

include_once "bs_mysql.php";

// Note, GetNewBuildID() will not be really used in this script, it will be in the import e-mail script
// $buildID = bs_get_new_buildID();

function bs_import_VariableEnd( $buildID, $logFileLocation ) {
	// Get the variables

	$bResult = file_exists($logFileLocation);
	if($bResult == FALSE) {
		echo "File not found: $logFileLocation! \n";
		exit(0);
	}

	$variableArray = bs_parse_VariableLog( "$logFileLocation" );

	// Ok, write out the information
	bs_write_mysql_variableLog( $buildID, $variableArray );

}

function bs_parse_VariableLog( $packageLocation ) {

	$bResult = file_exists($packageLocation);
	if($bResult == FALSE) {
		echo "File not found: $packageLocation! \n";
		exit(0);
	}
	$data = file_get_contents ( $packageLocation, FILE_TEXT );
	if( $data == false) {
		echo "Couldn't read from file $packageLocation \n";
		return FALSE;
	}

	// The XML format of this data string is.... not normal.... so I can't use simplexml to parse it
	$parser = xml_parser_create();
	if($parser == false) {
		$errCode = xml_get_error_code( $parser );
		$errString = xml_error_string( $errCode );
		echo "Error String: $errString \n";
		return FALSE;
	}


	$result = xml_parse_into_struct ( $parser, $data , &$xmlArray, &$variableIndex);
	if($result == false) {
		$errCode = xml_get_error_code ( $parser );
		$errString = xml_error_string( $errCode );
		echo "Error String: $errString \n";
		return FALSE;
	}

	$varIndexVarName = $variableIndex["VARNAME"];
	$varIndexVarValue = $variableIndex["VARVALUE"];

	$variableArray = array();
	// For each "VARNAME" 
	for ($i=0; $i < count($varIndexVarName); $i++) {
		$iIndexN = $varIndexVarName[$i];
		$iIndexV = $varIndexVarValue[$i];
		$aVariable = $xmlArray[$iIndexN];
		$aValue = $xmlArray[$iIndexV];
		$sVariable = $aVariable['value'];
		$sValue = $aValue['value'];
		$variableArray[$sVariable] = $sValue;
	}
	// Okay, that was FUGLY.... there has got to be a better way to do that...

	return $variableArray;
}

function bs_write_mysql_variableLog( $buildID, $variableArray ) {

	// Get host and IP from variableArray
	$sHostName = $variableArray["\$LOCALHOSTNAME\$"];
	$sHostIP = $variableArray["\$LOCALHOST\$"];
	$machine_id = get_machine_id( $sHostName, $sHostIP );

	// Now that we know the machine name, lets update the build with the machine index
	$link = bs_mysql_connect(); 
	$sql = "UPDATE `buildstatus`.`build` SET `machine` = \"$machine_id\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) {
	    die ('Query failed! : ' . mysql_error());
	}

	// Figure out based on "Project", "Build ID", and... ??? for location of build
	$sProject = $variableArray["\$PRODUCTNAME\$"];
	$sStartTime = $variableArray["\$BUILDSTARTTIME\$"];
	$package_location = "C:\\\\BuildStatus\\\\$sProject\\\\$sHostName\\\\$sStartTime";
	$sql = "UPDATE `buildstatus`.`build` SET `package_location` = \"$package_location\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// write out link directory of build
	$package_link = "$sProject/$sHostName/$sStartTime";
	$sql = "UPDATE `buildstatus`.`build` SET `package_link` = \"$package_link\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// BUILD START TIME
	$sql = "UPDATE `buildstatus`.`build` SET `build_start_time` = \"$sStartTime\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// PRODUCT NAME
	$sql = "UPDATE `buildstatus`.`build` SET `product_name` = \"$sProject\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// PATCHVERSION
	$sPatchVersion = $variableArray["\$PATCHVERSION\$"];
	// Okay, we know that 'some' builds haven't fully resolved this variable, so we are going to kludge it here
	$sPatchVersion = str_replace("\$BUILDSTARTTIME\$",$sStartTime, $sPatchVersion);
	if($sPatchVersion != "") { // Only present on production builds
		$sql = "UPDATE `buildstatus`.`build` SET `patch_version` = \"$sPatchVersion\" WHERE `build`.`build_id` = $buildID LIMIT 1";
		$bResult = mysql_query ( $sql );
		if(!$bResult) { die ('Query failed! : ' . mysql_error()); }
	}

	// COREGIMMEBRANCH
	$coregimmebranch = mysql_real_escape_string($variableArray["\$COREGIMMEBRANCH\$"]);
	$sql = "UPDATE `buildstatus`.`build` SET `gimme_branch_core` = \"$coregimmebranch\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// GIMMEBRANCH
	$data = $variableArray["\$GIMMEBRANCH\$"];
	$sql = "UPDATE `buildstatus`.`build` SET `gimme_branch` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// PATCHBRANCH
	$data = $variableArray["\$PATCHBRANCH\$"];
	$sql = "UPDATE `buildstatus`.`build` SET `gimme_branch_patch` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// GIMMETIME
	$data = $variableArray["\$GIMMETIME\$"];
	$sql = "UPDATE `buildstatus`.`build` SET `gimme_time` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// SVN_BRANCH
	$data = $variableArray["\$SVN_BRANCH\$"];
	$sql = "UPDATE `buildstatus`.`build` SET `svn_branch` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// SVNREVNUM
	$data = $variableArray["\$SVNREVNUM\$"];
	$sql = "UPDATE `buildstatus`.`build` SET `svn_revision` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// SVN_INCREMENTAL_SUMMARY
	$data = mysql_real_escape_string($variableArray["\$SVN_INCREMENTAL_SUMMARY\$"]);
	$sql = "UPDATE `buildstatus`.`build` SET `svn_incremental_summary` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// PUSH_COMPLETE
	$data = mysql_real_escape_string($variableArray["\$PUSH_COMPLETE\$"]);
	$sql = "UPDATE `buildstatus`.`build` SET `patch_push_complete` = \"$data\" WHERE `build`.`build_id` = $buildID LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { die ('Query failed! : ' . mysql_error()); }

	// TODO - Write out any other variables we are interested in

	mysql_close($link);
}


?> 
