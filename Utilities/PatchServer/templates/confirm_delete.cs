<?cs include:"headerxhtml.cs" ?>

<h1><a href="/"><?cs var:Server_Display_Name ?></a> - <?cs var:Title ?></h1>

<h2>Really delete this rule?</h2>

<?cs include:"display_rules.cs" ?>

<p>This will permanently disable and remove this rule.</p>

<?cs if:Type == "namedview" ?>
<form method="post" action="delete_namedview_rule">
<?cs else ?>
<form method="post" action="delete_branch_rule">
<?cs /if ?>
<div>
<input type="hidden" name="id" value="<?cs var:ID ?>"></input>
<input type="submit" value="Confirm Deletion"></input>
</div>
</form>

<?cs include:"footer.cs" ?>
