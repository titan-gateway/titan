# Titan Profiling Summary (Simple Mode)
**Generated:** Mon Dec 15 23:47:42 UTC 2025
**Duration:** 20s
**Binary:** ./build/release/src/titan
**Config:** config/benchmark.json

---

## Benchmark Results

```
Running 20s test @ http://localhost:8080/
  4 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.66ms    0.87ms  31.62ms   78.66%
    Req/Sec    18.93k     1.72k   39.96k    84.16%
  1510865 requests in 20.10s, 354.45MB read
Requests/sec:  75166.62
Transfer/sec:     17.63MB
```

---

## Binary Analysis

```
Binary Analysis
===============

Binary: ./build/release/src/titan
Size: 12M

Symbol Count: 24179

Key Symbols:
00000000004f69dc t .process_last_2blks
00000000004fa4f0 t .process_last_2blks
00000000004f5c34 t .process_last_2blks_gb
00000000004f9888 t .process_last_2blks_gb
000000000044b7c8 T ASN1_parse
000000000044b7e8 T ASN1_parse_dump
000000000029d000 T BIO_parse_hostserv
00000000002afec0 T CONF_parse_list
00000000004a832c T OSSL_HTTP_parse_url
00000000004a7c0c T OSSL_parse_url
00000000004d5080 T PKCS12_parse
00000000002478a0 T RECORD_LAYER_processed_read_pending
0000000000363cc8 T UI_process
00000000003768c8 T X509V3_parse_list
0000000000627c20 t ZSTD_optLdm_processMatchCandidate.part.0
00000000000b4360 t _GLOBAL__sub_I__ZN5titan7gateway17LoggingMiddleware16process_responseERNS0_15ResponseContextE
0000000000001580 u _ZGVZN7httplib6detail14FormDataParser5parseEPKcmRKSt8functionIFbRKNS_8FormDataEEERKS4_IFbS3_mEEE19re_rfc5987_encodingB5cxx11
00000000000015a8 u _ZGVZN7httplib6detail14FormDataParser5parseEPKcmRKSt8functionIFbRKNS_8FormDataEEERKS4_IFbS3_mEEE22re_content_dispositionB5cxx11
0000000000000190 u _ZGVZNK7httplib6Server18parse_request_lineEPKcRNS_7RequestEE7methodsB5cxx11
00000000005260e0 t _ZN3fmt3v126detail18parse_dynamic_specIcEENS1_25parse_dynamic_spec_resultIT_EEPKS4_S7_RiRNS1_7arg_refIS4_EERNS0_13parse_contextIS4_EE
None found
```

---

## Runtime Metrics

```
Runtime Metrics
===============

Process Info:
root      3921  345  0.2 393416 11156 ?        Sl   23:47   1:19 ./build/release/src/titan --config config/benchmark.json

Memory Usage:
VmPeak:	  458952 kB
VmSize:	  393416 kB
VmRSS:	   11156 kB

File Descriptors:
220
```

---

## Performance Summary

- **Throughput:** 75166.62 requests/sec
- **Avg Latency:** 2.66ms
- **Max Latency:** 31.62ms
- **Total Requests:** 1510865
- **Transfer Rate:** 17.63MB 

---

## Files Generated

- [benchmark.txt](benchmark.txt) - wrk benchmark output
- [binary_analysis.txt](binary_analysis.txt) - Binary symbol analysis
- [runtime_metrics.txt](runtime_metrics.txt) - Runtime process metrics
- [titan.log](titan.log) - Titan server log
