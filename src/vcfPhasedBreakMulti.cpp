
#include "vcf_parser.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/program_options.hpp>
using namespace std;
using namespace GTParser;
namespace po = boost::program_options;

// print out first cols
void printFirstCols(const vector<string> &firstCols, unsigned genomic_pos,
                    const string &alt) {

  assert(firstCols.size() == 8);
  cout << firstCols[0] << "\t" << genomic_pos << "\t.\t" << firstCols[2] << "\t"
       << alt;
  for (unsigned i = 4; i < 8; ++i)
    cout << "\t" << firstCols[i];
}

int main(int ac, char **av) {

  /*
    Code for defining input options.

   */

  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "malformedLinesFile", po::value<string>(),
      "Don't exit when encountering a malformed line.  Instead, write it to "
      "malformedLinesFile and move on.  All malformed lines are not "
      "printed to stdout")("keepInfo",
                           "Default behavior is to throw out the INFO fields "
                           "when a line is broken.  Turn on this flag to to "
                           "keep the old INFO line.  However, it likely won't "
                           "be accurate anymore.")(
      "recodeAsBiallelic", "Instead of breaking multiallelics into multiple "
                           "biallelics, just recode all alternate alleles as a "
                           "single allele");

  po::variables_map vm;
  po::store(po::parse_command_line(ac, av, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    cout << desc << "\n";
    return 1;
  }

  std::ofstream ofMalformedLines;
  if (vm.count("malformedLinesFile")) {
    string file = vm["malformedLinesFile"].as<string>();
    ofMalformedLines.open(file, std::ofstream::out | std::ofstream::trunc);
    cerr << "Writing malformed lines to " << file << endl;
  }

  const bool keepInfo = vm.count("keepInfo") > 0;
  const bool recodeAsBiallelic = vm.count("recodeAsBiallelic") > 0;

  // expect vcf formatted file as input
  string input;

  // pass on header
  unsigned lineNum(0);
  while (getline(cin, input)) {
    ++lineNum;
    if (input.size() < 2)
      continue;
    if (input[0] == '#' && input[1] == '#') {
      cout << input << "\n";
    } else if (input[0] == '#' && input[1] == 'C')
      break;
    else {
      cerr << "Unexpected line in header at line " << lineNum << endl;
      cerr << "Unexpected line: " << input << endl;
      exit(1);
    }
  }

  // count number of columns in chrom line
  ++lineNum;
  size_t numCols = std::count(input.begin(), input.end(), '\t') + 1;
  if (numCols < 7) {
    cerr << "Too few columns in VCF CHROM line (less than 7 columns)" << endl;
    cerr << "Malformed line: " << input << endl;
    exit(1);
  }
  // print crom line
  cout << input << "\n";

  // parse lines
  vcf_grammar<string::const_iterator> grammar(lineNum);
  while (getline(cin, input)) {
    ++lineNum;

    // don't parse this line if it's biallelic
    size_t startAlt = 0;
    for (unsigned i = 0; i < 4; ++i) {
      startAlt = input.find('\t', startAlt + 1);
    }
    size_t endAlt = input.find('\t', startAlt + 1);
    string alt = input.substr(startAlt + 1, endAlt - startAlt - 1);

    // ',' means this line has more than two alleles
    if (alt.find(',') == string::npos) {
      cout << input << "\n";
      continue;
    }

    // otherwise parse the line and do some splitting!

    // data to hold results of parsing
    // parsedFirstFewCols will hold columns 1-9, but excluding 2, which will be
    // in genomic_pos

    // #CHROM  POS     ID      REF     ALT     QUAL    FILTER  INFO    FORMAT
    // [SAMPLE1 .. SAMPLEN]
    vector<string> firstCols(8);
    unsigned genomic_pos;

    vector<t_genotype> genotypes;

    //
    // pos will point to where parsing stops.
    // Should == line.cend() if grammar is intended to consume whole line
    // Can check for true as well as "success"
    // Note that the destination of the parse are in a variadic list of
    // arguments (contig, genomic_pos etc.)
    // This list must be <= 9 (google SPIRIT_ARGUMENTS_LIMIT)
    //
    string::const_iterator pos = input.cbegin();
    try {

      qi::parse(pos, input.cend(), grammar, firstCols[0], genomic_pos,
                firstCols[1], firstCols[2], firstCols[3], firstCols[4],
                firstCols[5], firstCols[6], firstCols[7], genotypes);
    }
    catch (std::exception &e) {
      if (ofMalformedLines.is_open())
        ofMalformedLines << input << "\n";
      else {
        cerr << e.what() << endl;
        exit(1);
      }
    }
    vector<string> alts;
    boost::split(alts, firstCols[3], boost::is_any_of(","));
    assert(alts.size() > 1);

    // throw away info information as it is unlikely to still be true
    // unless told otherwise...
    if (!keepInfo)
      firstCols[6] = ".";

    if (recodeAsBiallelic) {

      // create new alternative allele that is a join of all alts by ":"
      string alt = alts[0];
      for (unsigned i = 1; i < alts.size(); ++i) {
        alt += ":";
        alt += alts[i];
      }
      // print out first cols
      printFirstCols(firstCols, genomic_pos, alt);

      // print out genotypes
      for (auto gg : genotypes) {
        cout << "\t";
        if (gg.allele1 != 0)
          cout << 1;
        else
          cout << 0;
        cout << gg.phase;
        if (gg.allele2 != 0)
          cout << 1;
        else
          cout << 0;
      }
      cout << "\n";

    } else {
      // loop to print out each alternate allele's own line
      unsigned altNum = 0;
      for (auto alt : alts) {
        ++altNum;

        // print out first cols
        printFirstCols(firstCols, genomic_pos, alt);

        // print out genotypes
        for (auto gg : genotypes) {
          cout << "\t";
          if (gg.allele1 == altNum)
            cout << 1;
          else
            cout << 0;
          cout << gg.phase;
          if (gg.allele2 == altNum)
            cout << 1;
          else
            cout << 0;
        }
        cout << "\n";
      }
    }
  }

  return 0;
}
