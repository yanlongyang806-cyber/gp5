<?php 

include_once "bs_mysql.php";
include_once "bs_unzip.php";
include_once "bs_import-VariableEnd.php";
include_once "bs_import-StateEnd.php";

bs_do_for_all_packages();

function bs_steps_to_do($build_ID, $packageDirectory) {
//		bs_import_buildEmail($build_ID, $extractDir);
	$filename = "$packageDirectory\\StateEnd.txt";
	echo "Performing steps on build $build_ID \n";
	bs_import_StateEnd($build_ID, $filename);
	usleep(40000);
}

function bs_do_for_all_packages() {
	// This is a once only function to get stuff already imported.
	$link = bs_mysql_connect(); 
    // loop through all builds in the database and import their emails

	$sql = "SELECT * FROM `buildstatus`.`build` WHERE `build_id` = 6132 ORDER BY `build`.`build_id` ASC ";
//	$sql = "SELECT * FROM `buildstatus`.`build` ORDER BY `build`.`build_id` ASC";
	$result = mysql_query ( $sql );
	if(!$result) {
	    die ('Query $sql failed! : ' . mysql_error());
	}

	while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
		$build_ID = $row["build_id"];
		$sPackageLocation = get_build_package_location($build_ID);
		if(!$sPackageLocation) {
			echo "No package location found! \n";
			return;
		}
		// extract zip
		$zipFile = $sPackageLocation . "\\BuildLogs.ZIP";
		$extractDir = "C:\\temp\\buildpackage";
		bs_unzip( $zipFile, $extractDir );


		// THIS IS THE FUNCTION YOU ARE RUNNING ON ALL BUILDS
		bs_steps_to_do($build_ID, $extractDir);
		if(!is_resource($link)) { // Sometimes gets closed in called functions
			$link = bs_mysql_connect();
		}
		// END FUNCTION

		// cleanup zip
		$output = system("rmdir /S /Q $extractDir");
	}
	
	mysql_free_result($result);
	if(is_resource($link)) { // Sometimes gets closed in called functions
		mysql_close($link);
	}
}


?> 
