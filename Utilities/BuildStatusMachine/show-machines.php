<?
include_once "www-header.php";
include_once "php/my-library.php";
include_once "php/bs_mysql.php";

$link = bs_mysql_connect();

// If we are submitting:
$sql = "SELECT * FROM `machine`";
$result = mysql_query ( $sql );
if(!$result) {
	die ('Query failed! : ' . mysql_error());
}
while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
	$hostname = $row["host_name"];
	$new_show = $_GET["$hostname"];
	if($new_show)
	{
		$show = $row["show_machine"];
		if(strcmp($new_show, "hide") == 0)
		{
			if($show == "1") {
	//			echo "MACHINE $hostname changing from $show to $new_show <br>";
				set_show_machine($hostname, "0"); // set to hide
			} else {
	//			echo "MACHINE $hostname had no change from $show to $new_show <br>";
			}
		} else {
			// Defaulting to all other strings mean show
			if($show == "1") {
	//			echo "MACHINE $hostname had no change from $show to $new_show <br>";
			} else {
	//			echo "MACHINE $hostname changing from $show to $new_show <br>";
				set_show_machine($hostname, "1"); // set to show
			}
		}
	}
}
mysql_free_result($result);







$sql = "SELECT * FROM `machine`";
$result = mysql_query ( $sql );
if(!$result) {
	die ('Query failed! : ' . mysql_error());
}
print "<form>";
print "<table border=\"2\" cellspacing=\"0\" >";
print "<CAPTION>Show Machines</CAPTION>";
print "<tr><th width=\"200px\">Machine</th> <th width=\"110px\">Yes - No</th> <tr>";
while ($row = mysql_fetch_array($result, MYSQL_BOTH)) {
	$hostname = $row["host_name"];
	$ishow = $row["show_machine"];
//	echo "SHOW = $ishow";
	if($ishow == "1") {
		$checked_yes="checked=\"checked\"";
		$checked_no="";
	} else {
		$checked_yes="";
		$checked_no="checked=\"checked\"";
	}
	print "<tr> <td>$hostname</td> <td>";
	print "<input type=\"radio\" name=\"$hostname\" value=\"show\" $checked_yes>";
	print "<input type=\"radio\" name=\"$hostname\" value=\"hide\" $checked_no>";
	print "</td> </tr>";
}
print "</table><br>";
print "<input type=\"submit\" value=\"Save Changes\">";

print "</form>";



mysql_free_result($result);
mysql_close($link);

function set_show_machine($hostname, $value) {
	$sql = "UPDATE `buildstatus`.`machine` SET `show_machine` = \"$value \" WHERE `machine`.`host_name` = \"$hostname\" LIMIT 1";
	$bResult = mysql_query ( $sql );
	if(!$bResult) { 
		echo "Query failed! : " . mysql_error() . "\n";
	}
}




include_once "www-footer.php";
?>

