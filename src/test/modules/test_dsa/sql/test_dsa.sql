CREATE EXTENSION test_dsa;

<<<<<<< HEAD
SELECT test_dsa_random(3, 5, 1024, 4096, 'random');
SELECT test_dsa_random(3, 5, 1024, 4096, 'forwards');
SELECT test_dsa_random(3, 5, 1024, 4096, 'backwards');

SELECT count(*) from test_dsa_random_parallel(3, 5, 1024, 8192, 'random', 5);
SELECT count(*) from test_dsa_random_parallel(3, 5, 1024, 8192, 'forwards', 5);
SELECT count(*) from test_dsa_random_parallel(3, 5, 1024, 8192, 'backwards', 5);

SELECT test_dsa_oom();
=======
SELECT test_dsa_basic();
SELECT test_dsa_resowners();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
