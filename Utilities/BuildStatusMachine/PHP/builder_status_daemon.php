<?php
include_once "C:/BuildStatus/PHP/update_builder_status.php";

loop_forever();

function loop_forever() {
	while(!file_exists("c:\\BuildStatus\\php\\stopdaemons.txt")) {
		UpdateCurrentStatusFromAllBuilders();
		// Run every 10 seconds.
		echo "Sleeping... \n";
		sleep(2);
	}
}
?>

