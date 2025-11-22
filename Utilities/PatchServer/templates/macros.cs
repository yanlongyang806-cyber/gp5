<?cs def:format_time(secs) ?><?cs 
  if:secs < 60 ?><?cs var:secs ?>s<?cs
  elif: secs < 3600 ?><?cs var:secs / 60 ?>m<?cs 
  else ?><?cs var:secs / 3600 ?>h<?cs /if ?><?cs 
/def ?>

<?cs def:format_bytes(val) ?><?cs 
  if:val == -1 ?>&#8734;<?cs<?cs 
  elif:val < 1024 ?><?cs var:val ?>B<?cs
  elif:val < 1048576 ?><?cs var:val / 1024 ?>KiB<?cs
  elif:val < 1073741824 ?><?cs var:val / 1048576 ?>MiB<?cs 
  else ?><?cs var:val / 1073741824 ?>GiB<?cs /if ?><?cs 
/def ?>

<?cs def:format_bits_from_bytes(val) ?><?cs 
  if:val == -1 ?>&#8734;<?cs<?cs 
  elif:val < 1250 ?><?cs var:val * 8 ?>b<?cs
  elif:val < 1250000 ?><?cs var:val / 125 ?>Kb<?cs
  elif:val < 1250000000 ?><?cs var:val / 125000 ?>Mb<?cs 
  else ?><?cs var:val / 125000000 ?>Gb<?cs /if ?><?cs 
/def ?>
