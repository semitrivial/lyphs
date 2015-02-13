function g(x)
{
  return document.getElementById(x);
}

function inputkey(e)
{
  if ( e.keyCode=='13' && g('inputbox').value != '' )
    ajax_run();
}

function ajax_run()
{
  var query = g('inputbox').value.trim();

  if ( query == '' )
    return;

  query = encodeURIComponent( query );

  var url = '/' + g('pulldown').value + '/' + query;
  var xmlhttp;

  if ( window.XMLHttpRequest )
    xmlhttp = new XMLHttpRequest();
  else
    xmlhttp = new ActiveXObject('Microsoft.XMLHTTP');

  xmlhttp.onreadystatechange = function()
  {
    if ( xmlhttp.readyState == 4 && xmlhttp.status == 200 )
    {
      g('raw_results_results').innerHTML = xmlhttp.responseText;
      g('formatted_results').innerHTML = format_ajax_response(xmlhttp.responseText);
    }
    else if ( xmlhttp.readyState == 4 && xmlhttp.status != 200 )
    {
      g('raw_results_results').innerHTML = 'Problem connecting to server';
      g('formatted_results').innerHTML = 'There was a problem connecting to the server.';
    }
  }

  xmlhttp.open('get',url,true);
  xmlhttp.send();

  var display_url = "'/" + g("pulldown").value + "/'+encodeURIComponent('" + g("inputbox").value.trim() + "')";
  g('raw_results').innerHTML = "API url: "+htmlEscape(display_url)+"<div id='raw_results_results'>Loading...</div>";
  g('formatted_results').innerHTML = 'Loading...';
}

function htmlEscape(str)
{
  return String(str).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function pulldownchange()
{
  var val = g('pulldown').value;
  var box = g('inputbox');

  if ( val == 'iri' )
    box.placeholder = 'Enter IRI to look up';
  else if ( val == 'autocomplete' || val == 'autocomplete-case-insensitive' )
    box.placeholder = 'Enter incomplete label to look up';
  else
    box.placeholder = 'Enter label to look up';
}

function formatted_clicked()
{
  g('jsonoption').checked = false;
  g('formatted_results').style.display = 'block';
  g('raw_results').style.display = 'none';
}

function jsonoption_clicked()
{
  g('formatted').checked = false;
  g('formatted_results').style.display = 'none';
  g('raw_results').style.display = 'block';
}

function format_ajax_response(x)
{
  var parsed = JSON.parse(x);
  var retval = "<ul>";

  if ( parsed.Results.length == 0 )
    retval += "<li>(No results)</li>";
  else
    for ( var i = 0; i < parsed.Results.length; i++ )
      retval += "<li>" + parsed.Results[i] + "</li>";

  return retval + "</ul>";
}
