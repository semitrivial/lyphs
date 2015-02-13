var palette = "";
var layers_html = "";
var layer_ids = new Object();
var palette_ids = new Object();
var use_raw_response = false;


function g(x)
{
  return document.getElementById(x);
}

function inputkey(e, inputbox)
{
  if ( e==13 || e.keyCode=='13' )
    ajax_run(inputbox);
}

function ajax_run(inputbox)
{
  var query;
  var xmlhttp;
  var handle_parsed_data;

  if ( window.XMLHttpRequest )
    xmlhttp = new XMLHttpRequest();
  else
    xmlhttp = new ActiveXObject('Microsoft.XMLHTTP');

  if ( inputbox === 'material_lookup' )
  {
    if ( g(inputbox).value.trim() == '' )
      return;

    handle_parsed_data = material_to_palette;

    query = '/lyph/'+g(inputbox).value.trim();
  }
  else if ( inputbox === 'layer_adder' )
  {
    var materialid = g('layer_material').value.trim();

    if ( materialid == '' )
      return;

    handle_parsed_data = add_layer;

    query = '/makelayer/?material=' + encodeURIComponent( materialid );

    var thickness = g('thickness').value.trim();

    if ( thickness != '' )
      query = query + '&thickness=' + encodeURIComponent( thickness );

    var color = g('color').value.trim();

    if ( color != '' )
      query = query + '&color=' + encodeURIComponent( color );
  }
  else if ( inputbox === 'lyph_adder' )
  {
    var proposedname = g('lyphname').value.trim();

    handle_parsed_data = display_lyph;

    if ( proposedname == '' )
    {
      alert("Please specify a name for the lyph");
      return;
    }

    var lyphtype = g('lyphtype').value.trim().toLowerCase();

    if ( lyphtype == '' )
    {
      alert("Please specify a type (MIX or SHELL) for the lyph");
      return;
    }

    query = '/makelyph/?name=' + encodeURIComponent(proposedname) + '&type=' + encodeURIComponent(lyphtype);

    for( var i=1; ; i++ )
    {
      var lyr = g('layer'+i);

      if ( lyr === null )
      {
        if ( i <= 2 )
        {
          alert("Please specify at least two layers for the lyph");
          return;
        }
        else
          break;
      }

      query += '&layer'+i+'='+encodeURIComponent(layer_ids[i]);
    }
  }
  else if ( inputbox === 'display_lyph_by_id' )
  {
    query = '/lyph/' + encodeURIComponent(g('display_lyph_by_id').value);

    handle_parsed_data = display_lyph;
  }
  else if ( inputbox === 'view_all_lyphs' )
  {
    query = '/all_lyphs/';
    use_raw_response = true;
    handle_parsed_data = display_all_lyphs;
  }
  else if ( inputbox === 'shortest_path' )
  {
    query = '/lyphpath/?from='+encodeURIComponent(g('frombox').value)+'&to='+encodeURIComponent(g('tobox').value);
    use_raw_response = true;
    handle_parsed_data = display_lyphpath;
  }
  else if ( inputbox === 'new_edge' )
  {
    query = '/makelyphedge/?from='+encodeURIComponent(g('edgefrombox').value)+'&to='+encodeURIComponent(g('edgetobox').value);
    query += '&name='+encodeURIComponent(g('edgenamebox').value);
    query += '&type='+encodeURIComponent(g('edgetypebox').value);
    query += '&fma='+encodeURIComponent(g('edgefmabox').value);
    if ( g('edgelyphbox').value != '' )
      query += '&lyph='+encodeURIComponent(g('edgelyphbox').value);
    use_raw_response = true;
    handle_parsed_data = handle_new_edge;
  }

  xmlhttp.onreadystatechange = function()   
  {
    if ( xmlhttp.readyState == 4 && xmlhttp.status == 200 )
    {
      var parsed = JSON.parse( xmlhttp.responseText );

      if ( parsed.Error )
      {
        alert(parsed.Error);
        return;
      }
      else
      {
        if ( use_raw_response === true )
        {
          handle_parsed_data( xmlhttp.responseText );
          use_raw_response = false;
        }
        else
          handle_parsed_data( parsed );
      }
    }
    else if ( xmlhttp.readyState == 4 && xmlhttp.status != 200 )
      alert("Problem connecting to server");
  }

  xmlhttp.open('get',query,true);
  xmlhttp.send();
}

function layer_to_palette( x )
{
  var y = new Object();

  y.id = x.mtlid;
  y.name = x.mtlname;

  material_to_palette( y );
}

function material_to_palette( x )
{
  var i;

  if ( !x.hasOwnProperty('id') )
    return;

  var id = x.id;

  for ( i = 0; ; i++ )
  {
    var mtr = g('materialid'+i);

    if ( mtr === null )
      break;

    if ( palette_ids[i] == id )
      return;
  }

  palette = "<li>" +
              "&raquo; " +
              "<span onclick='g(\"layer_material\").value=palette_ids["+i+"];' id='materialid"+i+"'>" +
                id+" " +
                "("+x.name+") " +
              "</span> " +
              "<a href='javascript:void(0);' onclick='g(\"display_lyph_by_id\").value=\""+ id +"\";inputkey(13,\"display_lyph_by_id\");'>" +
                "View" +
              "</a>" +
            "</li>" + palette;

  palette_ids[i] = id;
  g('palette_list').innerHTML = palette;
}

function add_layer( x )
{
  add_layer_with_position( x, -1 );
}

function add_layer_with_position( x, pos )
{
  layer_to_palette( x );

  g('lyph_id').innerHTML = 'Lyph ID: N/A ("Create Lyph" to assign ID)';
  g('lyphname').value = '';
  g('lyphtype').value = '';

  if ( pos == -1 )
  {
    var i;

    for( i=1; ; i++ )
    {
      var lyr = g('layer'+i);

      if ( lyr === null )
        break;
    }

    pos = i;
  }

  var thickness;
  if ( x.hasOwnProperty( 'thickness' ) )
    thickness = x.thickness;
  else
    thickness = 'Unspecified';

  var color;
  if ( x.hasOwnProperty( 'color' ) && x.color != '' )
    color = x.color;
  else
    color = 'Unspecified';

  layers_html += "<li>" +
                   "Layer #"+pos+" (Layer id: <span id='layer"+pos+"'>"+x.id+"</span>)" +
                   "<ul>" +
                     "<li>" +
                       "Material: "+x.mtlid+" ("+htmlEscape(x.mtlname)+"); " +
                       "Thickness: "+thickness+"; Color: "+htmlEscape(color) +
                     "</li>" +
                   "</ul>" +
                 "</li>";

  layer_ids[pos] = x.id;
  g('layers_list').innerHTML = layers_html;  
}

function display_all_lyphs( x )
{
  g('view_all_lyphs_result').innerHTML = "<pre>"+htmlEscape( x )+"</pre>";
}

function display_lyphpath( x )
{
  g('shortest_path_results').innerHTML = "<pre>"+htmlEscape( x )+"</pre>";
}

function handle_new_edge( x )
{
  g('new_edge_results').innerHTML = "<pre>"+htmlEscape(x)+"</pre>";
}

function display_lyph( x )
{
  material_to_palette( x );

  clear_display();

  var i;

  for ( i = 0; i < x.layers.length; i++ )
    add_layer_with_position( x.layers[i], i+1 );

  g('lyph_id').innerHTML = "Lyph ID: "+x.id+" ("+x.name+"), type: "+x.type;
}

function htmlEscape(str)
{
  return String(str).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function clear_display()
{
  g('lyph_id').innerHTML = 'Lyph ID: N/A ("Create Lyph" to assign ID)';
  g('lyphname').value = '';
  g('lyphtype').value = '';
  g('layer_material').value = '';
  g('thickness').value = '';
  g('color').value = '';
  layer_ids = new Object();
  layers_html = "";
  g('layers_list').innerHTML = '';
}

function menuclick(x)
{
  $(".area").not("."+x+"_area").hide(500, function()
  {
    $("."+x+"_area").show(500);
  });  
}
