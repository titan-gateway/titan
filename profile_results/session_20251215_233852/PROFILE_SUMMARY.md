# Titan Profiling Summary (Simple Mode)
**Generated:** Mon Dec 15 23:39:08 UTC 2025
**Duration:** 10s
**Binary:** ./build/release/src/titan
**Config:** config/benchmark.json

---

## Benchmark Results

```
Running 10s test @ http://localhost:8080/
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   179.85ms  301.77ms   1.95s    87.75%
    Req/Sec   109.43     75.01   460.00     76.46%
  4171 requests in 10.09s, 10.02MB read
  Socket errors: connect 0, read 0, write 0, timeout 38
  Non-2xx or 3xx responses: 522
Requests/sec:    413.21
Transfer/sec:      0.99MB
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
root        13  7.6  0.5 2485608 22196 ?       S    23:34   0:20 python3 -m http.server 3001
root        56  6.8  0.2 393416 10168 ?        Sl   23:38   0:00 ./build/release/src/titan --config config/benchmark.json

Memory Usage:
VmPeak:	  393416 kB
VmSize:	  393416 kB
VmRSS:	   10168 kB

File Descriptors:
31
```

---

## Performance Summary

- **Throughput:** 413.21 requests/sec
- **Avg Latency:** 179.85ms
- **Max Latency:** 1.95s
- **Total Requests:** 4171
- **Transfer Rate:** 0.99MB 

---

## Files Generated

- [benchmark.txt](benchmark.txt) - wrk benchmark output
- [binary_analysis.txt](binary_analysis.txt) - Binary symbol analysis
- [runtime_metrics.txt](runtime_metrics.txt) - Runtime process metrics
- [titan.log](titan.log) - Titan server log
