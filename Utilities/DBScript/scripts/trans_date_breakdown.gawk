BEGIN {
	OFS = ","

	if (!game) {game = "NW"}

	if (game == "NW") {
		paid_cur = 24;
		promo_cur = 25;
		paid_header = "paid_nw";
		promo_header = "promo_nw";
	} else if (game == "STO") {
		paid_cur = 17;
		promo_cur = 18;
		steam_cur = 21;
		paid_header = "paid_sto";
		promo_header = "promo_sto";
		steam_header = "steam_sto";
	} else if (game == "CO") {
		paid_cur = 19;
		promo_cur = 20;
		steam_cur = 22;
		paid_header = "paid_co";
		promo_header = "promo_co";
		steam_header = "steam_co";
	} else {
		print "Enter a valid game name!";
		exit
	}
}

($8 == 15 || $8 == 16 || $8 == paid_cur || $8 == promo_cur || (steam_cur && $8 == steam_cur)) && $11 == game && $13 == "Micropurchase" {
	split($9, dateparts, " ");
	date = dateparts[1];

	if (!acct || acct != $1 || !trans || trans != $2) {
		count[date]++;
	}
	
	if ($8 == 15) {
		paid_sh[date] += $15;
	} else if ($8 == 16) {
		promo_sh[date] += $15;
	} else if ($8 == paid_cur) {
		paid_game[date] += $15;
	} else if ($8 == promo_cur) {
		promo_game[date] += $15;
	} else if (steam_cur && $8 == steam_cur) {
		steam_game[date] += $15;
	}

	acct = $1;
	trans = $2;
}

END {
	if (steam_cur) {
		print "date,count,paid_shared,promo_shared", paid_header, promo_header, steam_header
	} else {
		print "date,count,paid_shared,promo_shared", paid_header, promo_header
	}

	for (date in count) {
		if (!paid_sh[date]) { paid_sh[date] = 0; }
		if (!promo_sh[date]) { promo_sh[date] = 0; }
		if (!paid_game[date]) { paid_game[date] = 0; }
		if (!promo_game[date]) { promo_game[date] = 0; }
		if (steam_cur && !steam_game[date]) { steam_game[date] = 0; }

		if (steam_cur) {
			print date, count[date], paid_sh[date], promo_sh[date], paid_game[date], promo_game[date], steam_game[date]
		} else {
			print date, count[date], paid_sh[date], promo_sh[date], paid_game[date], promo_game[date]
		}
	}
}