<?php

include_once "bs_get_build_from_email.php";

loop_forever();

function loop_forever() {
	while(!file_exists("c:\\BuildStatus\\php\\stopdaemons.txt")) {
		get_all_builds_from_email();
		// Run every minute.
		echo "Sleeping... \n";
		sleep(60);
	}
}

?> 
