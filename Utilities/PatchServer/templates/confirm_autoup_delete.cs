<?cs include:"headerxhtml.cs" ?>

<h1><a href="/"><?cs var:Server_Display_Name ?></a> - <?cs var:Title ?></h1>

<h2>Really delete this rule?</h2>

<?cs include:"display_autoupconf.cs" ?>

<p>This will permanently disable and remove this rule.</p>

<form method="post" action="delete_autoup_rule">
<div>
<input type="hidden" name="id" value="<?cs var:ID ?>"></input>
<input type="submit" value="Confirm Deletion"></input>
</div>
</form>

<?cs include:"footer.cs" ?>
