<?cs include:"headerxhtml.cs" ?>

<h1><a href="/"><?cs var:Server_Display_Name ?></a> - <?cs var:Title ?></h1>

<?cs include:"display_autoupconf.cs" ?>

<p>Please verify the rule data for accuracy.  If everything is correct, select "Create Rule." </p>

<form method="post" action="add_autoup_rule_verified"><div>
	<input type="hidden" name="id" value="<?cs var:ID ?>"></input>
	<input type="submit" value="Create Rule"></input>
</div></form>

<?cs include:"footer.cs" ?>
