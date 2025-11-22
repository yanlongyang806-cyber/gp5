<?php

function bs_unzip( $zipfile, $extract_dir ) {

	$bResult = file_exists($zipfile);
	if($bResult == FALSE) {
		echo "Couldn't find zip file $zipfile ! \n";
		return FALSE;
	}

	if($zip) {
		echo "Zip resource already present! \n";
	}

	$zip = new ZipArchive();
	if($zip == FALSE) {
	  echo "Failed to create new zip archive \n";
		return FALSE;
	}

	$result = $zip->open($zipfile);
	if ($result == FALSE) {
		echo "Zip failed to open, error code : $result \n";
		exit(0);
	}

	clearstatcache();
	system("rmdir /S /Q $extract_dir");
//	if(is_dir($extract_dir)){
//		echo "DEBUG: Directory $extract_dir EXISTS \n";
//		exit(0);
//	}
//	else {
//		echo "DEBUG: Directory $extract_dir DOES NOT EXIST \n";
//	}

    $result = $zip->extractTo($extract_dir);
	if ($result == FALSE) {
		echo "failed to extract $zipfile to $extract_dir $result \n";
		return $result;
	}
    $zip->close();
}

?> 