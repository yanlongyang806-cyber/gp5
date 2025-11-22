<?cs include:"header.cs" ?>
<?cs include:"macros.cs" ?>

<?cs def:checked(value) ?><?cs if:#value == 1 ?> checked="checked"<?cs /if ?><?cs /def ?>

<script type="text/javascript">
	if(typeof console === "undefined"){ console = { log: function() { } }; }
	<?cs include:"drill-down-table.cs" ?>
	console.log((new Date()) + ' Top of page has loaded...');
	$(document).ready(function()
	{
		$('input[type=button]').click(function()
		{
			$('input[name=csv]').attr('disabled', false);
			$('form').submit();
		});
		$('form').submit(function()
		{
			$('input[type=submit], input[type=button]').attr('disabled', true);
		});
		if(window.location.href.match(/collapse/))
		{
			var directory = '';
			console.log((new Date()) + ' Adding directories...');
			$('tr').each(function()
			{
				var path = $(this).find('td:last').text().replace(/^[\s]+|[\s]+$/g, '');
				var dir = path.replace(/[^/]+?$/, '');
				if(dir != directory)
				{
					$(this).before('<tr><td></td><td></td><td></td><td></td><td>' + dir + '</td></tr>');
					directory = dir;
				}
			});
			$('input[name=collapse]').attr('checked', true);
			console.log((new Date()) + ' Making drill-down table...');
			$('#broken-links').drillDownTable(5, 1); // lol the 5th column.
		}
		$('#broken-links').show();
		$('#loading-message').hide();
		$('input[type=submit], input[type=button]').attr('disabled', false);
		console.log((new Date()) + ' Done.');
	});
</script>

<style type="text/css">
	fieldset {
		padding: 5px;
	}
	legend {
		font-weight: bold;
	}
	p {
		margin: 5px;
	}
	label {
		width: 125px;
		text-align: right;
	}
	table {
		border-collapse: collapse;
		width: 100%;
	}
	fieldset, table, tr, th, td {
		border: 1px solid #666;
	}
	th {
		vertical-align: middle;
	}
	tr:nth-child(2) th {
		width: 132px;
	}
	tr:nth-child(2) th:nth-child(5) {
		width: 100%;
	}
	tbody tr:hover {
		background-color: #AAA !important;
	}
	.directory {
		cursor: pointer;
	}
	.directory td {
		border-left: 0;
		border-right: 0;
	}
	.directory td:nth-child(4) {
		background-image: url(data:image/png;base64,R0lGODlhEwAmAIAAAAAAAAAAACH5BAEAAAEALAAAAAATACYAAAI/jI+pB+vc3gJUKlotxNpgrn2gJI5OaV6otbJi98IpmXVBbUf2zvf+DwwKg6hizVhKIGc35MPoikVx0Z5uGCwAADs=);
		background-position: 115px -18px;
		background-repeat: no-repeat;
	}
	.collapsed td:nth-child(4) {
		background-position: 115px 3px;
	}
	.collapsed {
		font-weight: bold;
	}
</style>

<h1><a href="/"><?cs var:Config.Serverdisplayname ?></a> - Link Breaks (<?cs var:Config.Database[1].Name ?>)</h1>

<form method="get">
	<input type="hidden" name="project" value="<?cs var:Project ?>" />
	<input type="hidden" name="csv" disabled="disabled" value="1" />
	<fieldset>
		<legend>Options</legend>
		<p>
			<label>Lower Branch:</label>
			<input type="text" name="branch1" value="<?cs var:Branch1 ?>" />
		</p>
		<p>
			<label>Higher Branch:</label>
			<input type="text" name="branch2" value="<?cs var:Branch2 ?>" />
		</p>
		<p>
			<label>Prefix:</label>
			<input type="text" name="prefix" value="<?cs var:html_escape(Prefix) ?>" />
		</p>
		<p>
			<input type="checkbox" name="lost_checkins" value="1"<?cs call:checked(Lost_Checkins) ?> />
			Only show lost checkins, i.e. a change to a lower numbered branch, after a change to a higher numbered branch.
		</p>
		<p>
			<input type="checkbox" name="exclude_automations" value="1"<?cs call:checked(Exclude_Automations) ?> />
			Exclude checkins done by automation, i.e. builders, beaconizer, etc.
		</p>
		<p>
			<input type="checkbox" name="collapse" value="1" />
			Collapse directories.
		</p>
		<p>
			<input type="submit" value="Submit" disabled="disabled" />
			<input type="button" value="Download" disabled="disabled" />
		</p>
	</fieldset>
</form>

<br />

<?cs if:subcount(Broken_Links) ?>
	<fieldset id="loading-message">Loading, please wait...</fieldset>
	<table id="broken-links" style="display: none;">
		<thead>
			<tr>
				<th colspan="2">Revision</th>
				<th colspan="2">Author</th>
				<th rowspan="2">File Path</th>
			</tr>
			<tr>
				<th>Branch <?cs var:Branch1 ?></th>
				<th>Branch <?cs var:Branch2 ?></th>
				<th>Branch <?cs var:Branch1 ?></th>
				<th>Branch <?cs var:Branch2 ?></th>
			</tr>
		</thead>
		<tbody>
			<?cs each:Broken_Link = Broken_Links ?>
				<tr>
					<td data-comment="<?cs var:html_escape(Broken_Link.Checkin1.Comment) ?>">
						<a href="/<?cs var:Config.Database[1].Name ?>/checkin/<?cs var:Broken_Link.Ver1.Rev ?>" target="_blank">
							<?cs var:Broken_Link.Ver1.Rev ?>
						</a>
					</td>
					<td data-comment="<?cs var:html_escape(Broken_Link.Checkin2.Comment) ?>">
						<a href="/<?cs var:Config.Database[1].Name ?>/checkin/<?cs var:Broken_Link.Ver2.Rev ?>" target="_blank">
							<?cs var:Broken_Link.Ver2.Rev ?>
						</a>
					</td>
					<td>
						<a href="/<?cs var:Config.Database[1].Name ?>/user/<?cs var:Broken_Link.Checkin1.Author ?>" target="_blank">
							<?cs var:Broken_Link.Checkin1.Author ?>
						</a>
					</td>
					<td>
						<a href="/<?cs var:Config.Database[1].Name ?>/user/<?cs var:Broken_Link.Checkin2.Author ?>" target="_blank">
							<?cs var:Broken_Link.Checkin2.Author ?>
						</a>
					</td>
					<td>
						<a href="/<?cs var:Config.Database[1].Name ?>/file/<?cs var:url_escape(Broken_Link.Path) ?>" target="_blank">
							<?cs var:Broken_Link.Path ?>
						</a>
					</td>
				</tr>
			<?cs /each ?>
		</tbody>
	</table>
<?cs else ?>
	<fieldset>
		No broken links found matching search criteria.
	</fieldset>
<?cs /if ?>

<br />

<?cs include:"footer.cs" ?>
