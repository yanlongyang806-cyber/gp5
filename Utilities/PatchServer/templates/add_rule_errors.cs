<?cs include:"headerxhtml.cs" ?>

<h1><a href="/"><?cs var:Server_Display_Name ?></a> - <?cs var:Title ?></h1>

<h2>This rule has the following problems:</h2>

<ul>
	<?cs each:error = Errors ?>
		<li><?cs var:error ?></li>
	<?cs /each ?>
</ul>

<?cs include:"footer.cs" ?>
