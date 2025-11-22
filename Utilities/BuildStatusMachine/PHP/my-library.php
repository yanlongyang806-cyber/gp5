<?php


///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Some common/popular calls
///////////////////////////////////////////////////////////////////////////////////////////////////////////
function prettyformat_time($timeInSeconds) {
	$days = floor($timeInSeconds /(24*60*60));
	$hours = floor($timeInSeconds /(60*60)) % 24;
	$minutes = floor($timeInSeconds / 60) % 60;
	$seconds = $timeInSeconds % 60;
	if($days != 0) { $days = sprintf("%dd", $days); } else { $days = ""; }
	if($hours != 0 || $days !=0) { $hours = sprintf("%dh", $hours); } else { $hours = ""; }
	$minutes = sprintf("%d", $minutes);
//	$minutes = sprintf("%'02d", $minutes);
	$seconds = sprintf("%'02d", $seconds);
	return sprintf("%s %s %s:%s", $days, $hours,$minutes,$seconds);
}



?> 
