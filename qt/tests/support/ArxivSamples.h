/*
 * xournal-qt: answers of arXiv's export API as it sends them (saved; no test touches the network), for the citation
 * tests (qt/docs/citations.md).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

namespace xqt::test {

/// https://export.arxiv.org/api/query?search_query=ti:attention+AND+ti:all+AND+ti:you+AND+ti:need&start=0&max_results=10
/// (shortened to three entries; the second title is broken over two lines, as arXiv sends long titles)
inline constexpr const char* ARXIV_SEARCH = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <link href="http://arxiv.org/api/query?search_query%3Dti%3Aattention%20AND%20ti%3Aall%20AND%20ti%3Ayou%20AND%20ti%3Aneed%26id_list%3D%26start%3D0%26max_results%3D10" rel="self" type="application/atom+xml"/>
  <title type="html">ArXiv Query: search_query=ti:attention AND ti:all AND ti:you AND ti:need&amp;id_list=&amp;start=0&amp;max_results=10</title>
  <id>http://arxiv.org/api/0B7vR1qS0hVn2Hc6m3gJ3aYc6kE</id>
  <updated>2026-09-26T00:00:00-04:00</updated>
  <opensearch:totalResults xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/">3</opensearch:totalResults>
  <opensearch:startIndex xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/">0</opensearch:startIndex>
  <opensearch:itemsPerPage xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/">10</opensearch:itemsPerPage>
  <entry>
    <id>http://arxiv.org/abs/1706.03762v7</id>
    <updated>2023-08-02T00:41:18Z</updated>
    <published>2017-06-12T17:57:34Z</published>
    <title>Attention Is All You Need</title>
    <summary>  The dominant sequence transduction models are based on complex recurrent or
convolutional neural networks in an encoder-decoder configuration. The best
performing models also connect the encoder and decoder through an attention
mechanism.
</summary>
    <author>
      <name>Ashish Vaswani</name>
    </author>
    <author>
      <name>Noam Shazeer</name>
    </author>
    <author>
      <name>Niki Parmar</name>
    </author>
    <author>
      <name>Jakob Uszkoreit</name>
    </author>
    <author>
      <name>Llion Jones</name>
    </author>
    <arxiv:comment xmlns:arxiv="http://arxiv.org/schemas/atom">15 pages, 5 figures</arxiv:comment>
    <link href="http://arxiv.org/abs/1706.03762v7" rel="alternate" type="text/html"/>
    <link title="pdf" href="http://arxiv.org/pdf/1706.03762v7" rel="related" type="application/pdf"/>
    <arxiv:primary_category xmlns:arxiv="http://arxiv.org/schemas/atom" term="cs.CL" scheme="http://arxiv.org/schemas/atom"/>
    <category term="cs.CL" scheme="http://arxiv.org/schemas/atom"/>
    <category term="cs.LG" scheme="http://arxiv.org/schemas/atom"/>
  </entry>
  <entry>
    <id>http://arxiv.org/abs/2010.13154v2</id>
    <updated>2021-03-08T15:35:10Z</updated>
    <published>2020-10-25T16:54:24Z</published>
    <title>Attention is All You Need in Speech Separation: a Study of
  Transformers</title>
    <summary>  Recurrent Neural Networks (RNNs) have long been the dominant architecture in
sequence-to-sequence learning.
</summary>
    <author>
      <name>Cem Subakan</name>
    </author>
    <author>
      <name>Mirco Ravanelli</name>
    </author>
    <link href="http://arxiv.org/abs/2010.13154v2" rel="alternate" type="text/html"/>
    <link title="pdf" href="http://arxiv.org/pdf/2010.13154v2" rel="related" type="application/pdf"/>
    <arxiv:primary_category xmlns:arxiv="http://arxiv.org/schemas/atom" term="eess.AS" scheme="http://arxiv.org/schemas/atom"/>
  </entry>
  <entry>
    <id>http://arxiv.org/abs/hep-th/9901001v1</id>
    <updated>1999-01-04T10:00:00Z</updated>
    <published>1999-01-04T10:00:00Z</published>
    <title>An Old-Style Identifier</title>
    <summary>An entry with an identifier of the old style.</summary>
    <author>
      <name>A. Physicist</name>
    </author>
    <link href="http://arxiv.org/abs/hep-th/9901001v1" rel="alternate" type="text/html"/>
    <link title="pdf" href="http://arxiv.org/pdf/hep-th/9901001v1" rel="related" type="application/pdf"/>
  </entry>
</feed>
)";

/// https://export.arxiv.org/api/query?id_list=1234.5 (an ID arXiv does not know the form of)
inline constexpr const char* ARXIV_ERROR = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/">
  <link xmlns="http://www.w3.org/2005/Atom" href="http://arxiv.org/api/query?search_query=&amp;id_list=1234.5" rel="self" type="application/atom+xml"/>
  <title xmlns="http://www.w3.org/2005/Atom" type="html">ArXiv Query: search_query=&amp;id_list=1234.5</title>
  <id xmlns="http://www.w3.org/2005/Atom">http://arxiv.org/api/6pS1Tww9qqkI/2a4MuDUjrLbFNs</id>
  <updated xmlns="http://www.w3.org/2005/Atom">2026-09-26T00:00:00-04:00</updated>
  <opensearch:totalResults>1</opensearch:totalResults>
  <entry xmlns="http://www.w3.org/2005/Atom">
    <id>http://arxiv.org/api/errors#incorrect_id_format_for_1234.5</id>
    <title>Error</title>
    <summary>incorrect id format for 1234.5</summary>
    <updated>2026-09-26T00:00:00-04:00</updated>
    <link href="http://arxiv.org/api/errors#incorrect_id_format_for_1234.5" rel="alternate" type="text/html"/>
    <author>
      <name>arXiv api core</name>
    </author>
  </entry>
</feed>
)";

/// A search without results
inline constexpr const char* ARXIV_EMPTY = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title type="html">ArXiv Query: search_query=ti:qwertzuiop&amp;id_list=&amp;start=0&amp;max_results=10</title>
  <id>http://arxiv.org/api/xyz</id>
  <opensearch:totalResults xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/">0</opensearch:totalResults>
</feed>
)";

}  // namespace xqt::test
