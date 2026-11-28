"""tools/add_book.py with Storage, voice and tokenizer faked, so it runs with plain python3:

    python3 -m unittest tests.test_add_book
"""
import array
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import add_book  # noqa: E402  (also puts api/ on the path)
import _plumbing  # noqa: E402
import lumpi  # noqa: E402

FILES = {}  # storage path -> bytes, standing in for the bucket
SPOKEN = []


def fake_supabase(method, path, body=None, headers=None):
    if method == "POST":
        FILES[path] = body
        return b"{}"
    return FILES.get(path)


class FakeTokenizer:
    def phonemize(self, word, phonemizer_lang):
        return " ".join(word) + " |"  # "cat" -> "c a t |"


def fake_speak(voice, word):
    SPOKEN.append(word)
    return array.array("h", [1000, -1000] * 800)  # 0.1 s of sound


def add(pages, uid="04A1B2C3"):
    request = {"uid": uid, "title": "Cats", "language": "en-gb", "pages": pages}
    return add_book.add_book(request, FakeTokenizer(), lambda: None)


class AddBookTest(unittest.TestCase):
    def setUp(self):
        FILES.clear()
        SPOKEN.clear()
        self.real = _plumbing.supabase, add_book.speak
        _plumbing.supabase, add_book.speak = fake_supabase, fake_speak

    def tearDown(self):
        _plumbing.supabase, add_book.speak = self.real  # test_cloud.py has its own fake

    def test_adds_pages_and_speaks_each_word_once(self):
        add({"1": "The cat sat.", "2": "The cat!"})
        self.assertEqual(sorted(SPOKEN), ["cat", "sat", "the"])
        add({"3": "A cat."})  # a later page: only the new word is spoken
        self.assertEqual(SPOKEN[-1], "a")
        book = _plumbing.load_book("04A1B2C3")
        self.assertEqual(sorted(book["passages"]), ["1", "2", "3"])
        self.assertEqual(book["passages"]["1"]["words"][1], {"word": "cat", "ipa": "c a t"})
        self.assertEqual(lumpi.book("04A1B2C3"), "Cats")  # the cloud reads the stored book

    def test_split_words(self):
        self.assertEqual(add_book.split_words(1, "Don’t stop, well-known cat!"), ["don't", "stop", "well", "known", "cat"])
        for text in ["3 cats", "café", "", "a " * 501]:
            with self.assertRaises(ValueError):
                add_book.split_words(1, text)

    def test_bad_requests_upload_nothing(self):
        for pages, uid in [({"01": "Cat."}, "04A1B2C3"), ({"1": "Cat 3."}, "04A1B2C3"), ({"1": "Cat."}, "../x")]:
            with self.assertRaises(ValueError):
                add(pages, uid)
        self.assertEqual(FILES, {})


if __name__ == "__main__":
    unittest.main()
