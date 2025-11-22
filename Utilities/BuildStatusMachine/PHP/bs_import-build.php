<?php

include_once "bs_import-VariableEnd.php";
include_once "bs_import-buildemail.php";
include_once "bs_import-StateEnd.php";

function bs_import_build($buildID, $extract_dir) {

	// NOTE: bs_import_VariableEnd MUST be called first as it sets the package location used by the other scripts.
	$filename = "$extract_dir/VariablesEnd.txt";
	bs_import_VariableEnd( $buildID, $filename );

	bs_import_buildEmail( $buildID, $extract_dir );

	$filename = "$extract_dir/StateEnd.txt";
	bs_import_StateEnd($buildID, $filename);

}
?> 
