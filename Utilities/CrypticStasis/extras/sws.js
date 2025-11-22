function sendXHR(url, callback) {
    var req;

	req = false;
    // branch for native XMLHttpRequest object
    if(window.XMLHttpRequest && !(window.ActiveXObject)) {
    	try {
			req = new XMLHttpRequest();
        } catch(e) {
			req = false;
        }
    // branch for IE/Windows ActiveX version
    } else if(window.ActiveXObject) {
       	try {
        	req = new ActiveXObject("Msxml2.XMLHTTP");
      	} catch(e) {
        	try {
          		req = new ActiveXObject("Microsoft.XMLHTTP");
        	} catch(e) {
          		req = false;
        	}
		}
    }
    
	if(req) {
	    req.onreadystatechange = function() {
            // only if req shows "loaded"
            if (req.readyState == 4) {
                // only if "OK"
                if (!req.status || req.status == 200) {
                    callback(req);
                }
            }
        };
        
		req.open("GET", url, true);
		req.send("");
	}
}

function toggleTableSection(secName, url) {
    // 'url' is for death reports
    // it would work with anything else but it isn't used for that
    
    var trs = document.getElementsByTagName('tr');
    for( var x = 0 ; x < trs.length ; x ++ ) {
        att = trs[x].getAttribute('name');
        if( (trs[x].className == 's' || trs[x].className == 'sectionSlave') && att == 's' + secName ) {
            if( trs[x].style.display == 'none' || (!trs[x].style.display && trs[x].className == 's') ) {
                // Making things visible.
                
                if( url && ! trs[x].cells[0].innerHTML ) {
                    var success = function(o) {
                        if( o.responseText !== undefined ) {
                            var autopsy = eval( "(" + o.responseText + ")" );
                            
                            var xi = 0;
                            var trs = document.getElementsByTagName('tr');
                            for( var x = 0 ; x < trs.length ; x ++ ) {
                                att = trs[x].getAttribute('name');
                                if( att == 's' + secName ) {
                                    var tmin = Math.floor(autopsy[xi].t / 60);
                                    var tsec = Math.floor(autopsy[xi].t % 60);
                                    var tms  = Math.floor(( autopsy[xi].t - Math.floor( autopsy[xi].t ) ) * 1000);
                                    if( tms < 10 ) {
                                        tms = "00" + tms;
                                    } else if( tms < 100 ) {
                                        tms = "0" + tms;
                                    }

                                    trs[x].cells[ trs[x].cells.length - 3 ].innerHTML = ( tmin < 10 ? '0' + tmin.toString() : tmin.toString() ) + ":" + ( tsec < 10 ? '0' + tsec.toString() : tsec.toString() ) + "." + tms;

                                    if( autopsy[xi].hp != "0" ) {
                                        trs[x].cells[ trs[x].cells.length - 2 ].innerHTML = autopsy[xi].hp;
                                    } else {
                                        trs[x].cells[ trs[x].cells.length - 2 ].innerHTML = "";
                                    }

                                    trs[x].cells[ trs[x].cells.length - 1 ].innerHTML = autopsy[xi++].str;
                                }
                            }
                            
                            showTableSection(secName);
                        }
                    };
                    
                    sendXHR( url, success );
                } else {
                    showTableSection(secName);
                }
                
                return;
            } else {
                // Making things invisible.
                hideTableSection(secName);
                return;
            }
        }
    }
}

function showTableSection(secName) {
    var trs = document.getElementsByTagName('tr');
    for( var x = 0 ; x < trs.length ; x ++ ) {
        att = trs[x].getAttribute('name');
        if( (trs[x].className == 's' || trs[x].className == 'sectionSlave') && att == 's' + secName ) {
            // Making things visible.
            try {
    			trs[x].style.display = 'table-row';
    		} catch (e) {
    			// for IE
    			trs[x].style.display = 'block';
    		}
        }
    }
    
    a = document.getElementById('as'+secName);
    
    if( a ) {
        a.innerHTML = '-';
    }
}

function hideTableSection(secName) {
    var trs = document.getElementsByTagName('tr');
    for( var x = 0 ; x < trs.length ; x ++ ) {
        att = trs[x].getAttribute('name');
        if( (trs[x].className == 's' || trs[x].className == 'sectionSlave') && att == 's' + secName ) {
            trs[x].style.display = 'none';
        }
    }
    
    a = document.getElementById('as'+secName);
    
    if( a ) {
        a.innerHTML = '+';
    }
}

function toggleTab(tabId,front) {
    /* Bail out if we're supposed to be on the front page but we're not. */
    if( front && !document.getElementById('tab_damage_out') ) return;
    
    /* Check if the target tab exists. */
    var div = document.getElementById('tab_' + tabId);
    var a = document.getElementById('tablink_' + tabId);
    
    if( div && a ) {
        /* Find other tabs and turn them off. */
        var divs = document.getElementsByTagName('div');
        for( var x = 0 ; x < divs.length ; x ++ ) {
            if( divs[x].className == 'tab' ) {
                divs[x].style.display = 'none';
            }
        }
        
        /* Disable other tab links. */
        var as = document.getElementsByTagName('a');
        for( var x = 0 ; x < as.length ; x ++ ) {
            if( as[x].className == 'tabLink select' ) {
                as[x].className = 'tabLink';
            }
        }
        
        /* Enable ours. */
        div.style.display = 'block';
        a.className = 'tabLink select';
    }
    
    return false;
}

/* Even though this is called hashTab, it does a bunch of other page-load type things. */
function hashTab() {
    /* Figure out what tab we're on. */
    var t = location.hash.substring(1);
    if( t.length > 0 ) {
        var x = t.indexOf('0');
        if( x > 0 ) {
            toggleTab( t.substring( 0, x ) );
            document.location.href = "#" + t;
        } else {
            toggleTab(t);
        }
    }
    
    /* Add yui-skin-sam to the body's class. */
    if(document.body.className) {
        document.body.className += ' yui-skin-sam';
    } else {
        document.body.className = 'yui-skin-sam';
    }
}

function initMenu(idMenu, jsonFiles) {
    /* Set up the top navigation menu. */
    YAHOO.util.Event.onContentReady(idMenu, function () {
        /* Standard menu items are marked up already */
        var oMenuBar = new YAHOO.widget.MenuBar(idMenu, { autosubmenudisplay: true, showdelay: 0, hidedelay: 750, lazyload: true, scrollincrement: 5 });
        
        for( var idMenuItem in jsonFiles ) {
            var jsonFile = jsonFiles[idMenuItem];
            
            if( document.getElementById(idMenuItem) ) {
                /* Use XHR to add boss navigation */
                sendXHR( jsonFile, function(o) {
                    if( o.responseText !== undefined ) {
                        try {
                            var splits = YAHOO.lang.JSON.parse(o.responseText);

                            if( splits.length > 0 ) {
                                var splitsData = [];
                                for( var x = 0 ; x < splits.length ; x++ ) {
                                    /* Boss name */
                                    var splitText = splits[x].long;
                                    
                                    /* Amount of damage */
                                    var dmgText = "";
                                    var n = splits[x].damage;
                                    
                                    if( n && n > 0 ) {
                                        dmgText = " (";

                                        if( n < 1000 ) {
                                            dmgText += Math.floor(n);
                                        } else if( n < 100000 ) {
                                            dmgText += Math.floor(n / 100)/10 + "K";
                                        } else {
                                            dmgText += Math.floor(n / 100000)/10 + "M";
                                        }

                                        dmgText += " dmg)";
                                    }

                                    splitsData.push( { "text": splitText + dmgText, "url": splits[x].dname } );
                                }
                                
                                var items = oMenuBar.getItems();
                                for( var xx in items ) {
                                    if( items[xx].id == idMenuItem ) {
                                        oMenuItem = items[xx];
                                    }
                                }
                                
                                oMenuItem.cfg.setProperty("submenu", { id: "splits", itemdata: splitsData });
                                oMenuBar.render();
                            }
                        } catch(e) {}
                    }
                });
                
            }
        }
        
        oMenuBar.render();
    });
}

function initTabs() {
    var spans = document.getElementsByTagName('span');
    var tips = [];
    for( var x = 0 ; x < spans.length ; x ++ ) {
        if( spans[x].className == 'tip' ) {
            var title = spans[x].getAttribute('title');
            spans[x].setAttribute('title', '<div class="swstip">' + title.replace( /;/g, "<br />" ) + '</div>' );
            tips[tips.length] = spans[x];
        }
    }
    
    if( tips.length > 0 ) {
        var myTooltip = new YAHOO.widget.Tooltip( "swstips", { context: tips, showdelay: 0, hidedelay: 0 } );
    }
}

/* Call hashTab when the page loads so we switch to the right tab. */
if(window.addEventListener) {
    window.addEventListener('load', hashTab, false);
} else if(window.attachEvent) {
    window.attachEvent('onload', hashTab);
}
