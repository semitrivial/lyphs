import java.io.File;
import java.io.PrintWriter;
import java.util.Set;
import org.semanticweb.owlapi.apibinding.OWLManager;
import org.semanticweb.owlapi.model.OWLOntologyManager;
import org.semanticweb.owlapi.model.OWLOntology;
import org.semanticweb.owlapi.model.OWLOntologyLoaderConfiguration;
import org.semanticweb.owlapi.model.*;
import org.semanticweb.owlapi.reasoner.*;
import org.semanticweb.owlapi.io.OWLOntologyDocumentSource;
import org.semanticweb.owlapi.io.FileDocumentSource;
import org.coode.owlapi.manchesterowlsyntax.ManchesterOWLSyntaxEditorParser;
import org.semanticweb.owlapi.util.BidirectionalShortFormProvider;
import org.semanticweb.owlapi.util.BidirectionalShortFormProviderAdapter;
import org.semanticweb.owlapi.util.SimpleShortFormProvider;
import org.semanticweb.owlapi.util.AnnotationValueShortFormProvider;
import org.semanticweb.owlapi.util.OWLOntologyImportsClosureSetProvider;
import org.semanticweb.owlapi.expression.OWLEntityChecker;
import org.semanticweb.owlapi.expression.ShortFormEntityChecker;
import org.semanticweb.elk.owlapi.ElkReasonerFactory;


public class Convert
{
  public static void main(String [] args) throws Exception
  {
    Convert c = new Convert();
    c.run(args);
  }

  public void run(String [] args) throws Exception
  {
    OWLOntologyManager manager = OWLManager.createOWLOntologyManager();
    OWLOntologyLoaderConfiguration config = new OWLOntologyLoaderConfiguration();
    config.setMissingOntologyHeaderStrategy(OWLOntologyLoaderConfiguration.MissingOntologyHeaderStrategy.IMPORT_GRAPH);

    File kbfile;
    OWLOntology ont;

    if ( args.length != 2 )
    {
      System.err.println( "Syntax: java Convert (owlfile) (outputfile)" );
      System.err.println( "..." );
      System.err.println( "For example: java Convert /home/ricordo/ontology/ricordo.owl ricordo.nt" );
      return;
    }

    try
    {
      kbfile = new File(args[0]);
      ont = manager.loadOntologyFromOntologyDocument(new FileDocumentSource(kbfile),config);
    }
    catch(Exception e)
    {
      System.out.println("could not open '"+args[0]+"' to read.");
      e.printStackTrace();
      return;
    }

    PrintWriter writer;

    try
    {
      writer = new PrintWriter( args[1], "UTF-8" );
    }
    catch( Exception e )
    {
      System.out.println( "Could not open '"+args[1]+"' to write." );
      e.printStackTrace();
      return;
    }

    OWLReasonerFactory rf = new ElkReasonerFactory();
    OWLReasoner r = rf.createReasoner(ont);

    IRI iri = manager.getOntologyDocumentIRI(ont);

    OWLDataFactory df = OWLManager.getOWLDataFactory();
    Set<OWLOntology> onts = ont.getImportsClosure();

    for ( OWLOntology o : onts )
    {
      Set<OWLClass> classes = o.getClassesInSignature();

      for ( OWLClass c : classes )
      {
        String cID = prepare_class_id(c.toString());
        boolean fLabel = false;

        Set<OWLAnnotation> annots = c.getAnnotations(o, df.getRDFSLabel() );
        for ( OWLAnnotation a : annots )
        {
          if ( a.getValue() instanceof OWLLiteral )
          {
            fLabel = true;
            writer.print( cID + " <http://www.w3.org/2000/01/rdf-schema#label> \"" + escape(((OWLLiteral)a.getValue()).getLiteral().trim()) + "\" .\n" );
          }
        }

        if ( !fLabel )
          continue;

        NodeSet<OWLClass> subClasses = r.getSubClasses(c, true);

        for ( Node<OWLClass> subnode : subClasses )
        {
          OWLClass sub = subnode.getEntities().iterator().next();

          if ( sub.isOWLNothing() )
            continue;

          writer.print( prepare_class_id(sub.toString()) + " <http://www.w3.org/2000/01/rdf-schema#subClassOf> " + cID + " .\n" );
        }
      }
    }

    writer.close();

    return;
  }

  String prepare_class_id(String id)
  {
    String retval = id.trim();

    if ( retval.startsWith( "owl:" ) )
      retval = "<http://www.w3.org/2002/07/owl#" + retval.substring(4) + ">";

    return retval;
  }

  String escape(String x)
  {
    StringBuilder sb;
    int i, len;

    len = x.length();
    sb = new StringBuilder(len);

    for ( i = 0; i < len; i++ )
    {
      switch( x.charAt(i) )
      {
        case '\\':
          sb.append( "\\\\" );
          break;
        case '\"':
          sb.append( "\\\"" );
          break;
        case '\n':
          sb.append( "\\n" );
          break;
        case '\r':
          sb.append( "\\r" );
          break;
        case '\t':
          sb.append( "\\t" );
          break;
        default:
          sb.append( x.charAt(i) );
          break;
      }
    }

    return sb.toString();
  }
}

