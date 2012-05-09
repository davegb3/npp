// This file is part of Notepad++ project
// Copyright (C)2003 Don HO <don.h@free.fr>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// Note that the GPL places important restrictions on "derived works", yet
// it does not provide a detailed definition of that term.  To avoid      
// misunderstandings, we consider an application to constitute a          
// "derivative work" for the purpose of this license if it does any of the
// following:                                                             
// 1. Integrates source code from Notepad++.
// 2. Integrates/includes/aggregates Notepad++ into a proprietary executable
//    installer, such as those produced by InstallShield.
// 3. Links to a library or executes a program that does any of the above.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "precompiledHeaders.h"
#include "xmlMatchedTagsHighlighter.h"
#include "ScintillaEditView.h"
/*
int XmlMatchedTagsHighlighter::getFirstTokenPosFrom(int targetStart, int targetEnd, const char *token, bool isRegex, pair<int, int> & foundPos)
{
	//int start = currentPos;
	//int end = (direction == DIR_LEFT)?0:_pEditView->getCurrentDocLen();
	
	_pEditView->execute(SCI_SETTARGETSTART, targetStart);
	_pEditView->execute(SCI_SETTARGETEND, targetEnd);
	_pEditView->execute(SCI_SETSEARCHFLAGS, isRegex ? (SCFIND_REGEXP|SCFIND_POSIX) : 0);
	int posFind = _pEditView->execute(SCI_SEARCHINTARGET, (WPARAM)strlen(token), (LPARAM)token);
	if (posFind != -1)
	{
		foundPos.first = _pEditView->execute(SCI_GETTARGETSTART);
		foundPos.second = _pEditView->execute(SCI_GETTARGETEND);
	}
	return posFind;
}

TagCateg XmlMatchedTagsHighlighter::getTagCategory(XmlMatchedTagsPos & tagsPos, int curPos)
{
	pair<int, int> foundPos;

	int docLen = _pEditView->getCurrentDocLen();

	int gtPos = getFirstTokenPosFrom(curPos, 0, ">", false, foundPos);
	int ltPos = getFirstTokenPosFrom(curPos, 0, "<", false, foundPos);
	if (ltPos != -1)
	{
		if ((gtPos != -1) && (ltPos < gtPos))
			return outOfTag;

		// Now we are sure about that we are inside of tag
		// We'll try to determinate the tag category :
		// tagOpen : <Tag>, <Tag Attr="1" >
		// tagClose : </Tag>
		// tagSigle : <Tag/>, <Tag Attr="0" />
		int charAfterLt = _pEditView->execute(SCI_GETCHARAT, ltPos+1);
		if (!charAfterLt)
			return unknownPb;

		if ((char)charAfterLt == ' ')
			return invalidTag;

		// so now we are sure we have tag sign '<'
		// We'll see on the right
		int gtPosOnR = getFirstTokenPosFrom(curPos, docLen, ">", false, foundPos);
		int ltPosOnR = getFirstTokenPosFrom(curPos, docLen, "<", false, foundPos);

		if (gtPosOnR == -1)
			return invalidTag;

		if ((ltPosOnR != -1) && (ltPosOnR < gtPosOnR))
			return invalidTag;

		if ((char)charAfterLt == '/')
		{
			int char2AfterLt = _pEditView->execute(SCI_GETCHARAT, ltPos+1+1);

			if (!char2AfterLt)
				return unknownPb;

			if ((char)char2AfterLt == ' ')
				return invalidTag;

			tagsPos.tagCloseStart = ltPos;
			tagsPos.tagCloseEnd = gtPosOnR + 1;
			return tagClose;
		}
		else
		{
			// it's sure for not being a tagClose
			// So we determinate if it's tagSingle or tagOpen
			tagsPos.tagOpenStart = ltPos;
			tagsPos.tagOpenEnd = gtPosOnR + 1;

			int charBeforeLt = _pEditView->execute(SCI_GETCHARAT, gtPosOnR-1);
			if ((char)charBeforeLt == '/')
				return inSingleTag;

			return tagOpen;
		}
	}
		
	return outOfTag;
}

bool XmlMatchedTagsHighlighter::getMatchedTagPos(int searchStart, int searchEnd, const char *tag2find, const char *oppositeTag2find, vector<int> oppositeTagFound, XmlMatchedTagsPos & tagsPos)
{
	const bool search2Left = false;
	const bool search2Right = true;

	bool direction = searchEnd > searchStart;

	pair<int, int> foundPos;
	int ltPosOnR = getFirstTokenPosFrom(searchStart, searchEnd, tag2find, true, foundPos);
	if (ltPosOnR == -1)
		return false;

	// if the tag is found in non html zone, we skip it
	const NppGUI & nppGUI = (NppParameters::getInstance())->getNppGUI();
	int idStyle = _pEditView->execute(SCI_GETSTYLEAT, ltPosOnR);
	if (!nppGUI._enableHiliteNonHTMLZone && (idStyle >= SCE_HJ_START || idStyle == SCE_H_COMMENT))
	{
		int start = (direction == search2Left)?foundPos.first:foundPos.second;
		int end = searchEnd;
		return getMatchedTagPos(start, end, tag2find, oppositeTag2find, oppositeTagFound, tagsPos);
	}

	TagCateg tc = outOfTag;
	if (direction == search2Left)
	{
		tc = getTagCategory(tagsPos, ltPosOnR+2);
		
		if (tc != tagOpen && tc != inSingleTag)
 			return false;
		if (tc == inSingleTag)
		{
			int start = foundPos.first;
			int end = searchEnd;
			return getMatchedTagPos(start, end, tag2find, oppositeTag2find, oppositeTagFound, tagsPos);
		}
	}

	pair<int, int> oppositeTagPos;
	int s = foundPos.first;
	int e = tagsPos.tagOpenEnd;
	if (direction == search2Left)
	{
		s = foundPos.second;
		e = tagsPos.tagCloseStart;
	}

	int ltTag = getFirstTokenPosFrom(s, e, oppositeTag2find, true, oppositeTagPos);

	if (ltTag == -1)
	{
		if (direction == search2Left)
		{
			return true;
		}
		else
		{
			tagsPos.tagCloseStart = foundPos.first;
			tagsPos.tagCloseEnd = foundPos.second;
			return true;
		}
	}
	else 
	{
		// RegExpr is "<tagName[ 	>]", found tag could be a openTag or singleTag
		// so we should make sure if it's a singleTag
		XmlMatchedTagsPos pos;
		if (direction == search2Right && getTagCategory(pos,ltTag+1) == inSingleTag)
		{
			for(;;)
			{
				ltTag = getFirstTokenPosFrom(ltTag, e, oppositeTag2find, true, oppositeTagPos);
				
				if (ltTag == -1)
				{
					tagsPos.tagCloseStart = foundPos.first;
					tagsPos.tagCloseEnd = foundPos.second;
					return true;
				}
				else 
				{
					if (getTagCategory(pos,ltTag+1) == inSingleTag)
					{
						continue;
					}

					if (!isInList(ltTag, oppositeTagFound))
					{
						oppositeTagFound.push_back(ltTag);
						break;
					}
				}
			}
			return getMatchedTagPos(foundPos.second, searchEnd, tag2find, oppositeTag2find, oppositeTagFound, tagsPos);
		}


		if (isInList(ltTag, oppositeTagFound))
		{
			for(;;)
			{
				ltTag = getFirstTokenPosFrom(ltTag, e, oppositeTag2find, true, oppositeTagPos);
				if (ltTag == -1)
				{
					if (direction == search2Left)
					{
						return true;
					}
					else
					{
						tagsPos.tagCloseStart = foundPos.first;
						tagsPos.tagCloseEnd = foundPos.second;
					}
					return true;
				}
				else if (!isInList(ltTag, oppositeTagFound))
				{
					oppositeTagFound.push_back(ltTag);
					break;
				}
				else
				{
					if (direction == search2Left)
					{
						XmlMatchedTagsPos tmpTagsPos;
						getTagCategory(tmpTagsPos, ltTag+1);
						ltTag = tmpTagsPos.tagCloseEnd;
					}
				}
			}
		}
		else
		{
			oppositeTagFound.push_back(ltTag);
		}
	}
	int start, end;
	if (direction == search2Left)
	{
		start = foundPos.first;
		end = searchEnd;
	}
	else
	{
		start = foundPos.second;
		end = searchEnd;
	}

	return getMatchedTagPos(start, end, tag2find, oppositeTag2find, oppositeTagFound, tagsPos);
}


bool XmlMatchedTagsHighlighter::getXmlMatchedTagsPos(XmlMatchedTagsPos & tagsPos)
{
	// get word where caret is on
	int caretPos = _pEditView->execute(SCI_GETCURRENTPOS);
	
	// if the tag is found in non html zone (include comment zone), then quit
	const NppGUI & nppGUI = (NppParameters::getInstance())->getNppGUI();
	int idStyle = _pEditView->execute(SCI_GETSTYLEAT, caretPos);
	if (!nppGUI._enableHiliteNonHTMLZone && (idStyle >= SCE_HJ_START || idStyle == SCE_H_COMMENT))
		return false;

	int docLen = _pEditView->getCurrentDocLen();

	// determinate the nature of current word : tagOpen, tagClose or outOfTag
	TagCateg tagCateg = getTagCategory(tagsPos, caretPos);

	static const char tagNameChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_:";

	switch (tagCateg)
	{
		case tagOpen : // if tagOpen search right
		{
			_pEditView->execute(SCI_SETWORDCHARS, 0, (LPARAM)tagNameChars);
			int startPos = _pEditView->execute(SCI_WORDSTARTPOSITION, tagsPos.tagOpenStart+1, true);
			int endPos = _pEditView->execute(SCI_WORDENDPOSITION, tagsPos.tagOpenStart+1, true);
			tagsPos.tagNameEnd = endPos;

			_pEditView->execute(SCI_SETCHARSDEFAULT);
			char * tagName = new char[endPos-startPos+1];

			_pEditView->getText(tagName, startPos, endPos);

			basic_string<char> closeTag = "</";
			closeTag += tagName;
			closeTag += "[ 	]*>";
			
			basic_string<char> openTag = "<";
			openTag += tagName;
			openTag += "[ 	>]";

			delete [] tagName;

			vector<int> passedTagList;
			return getMatchedTagPos(tagsPos.tagOpenEnd, docLen, closeTag.c_str(), openTag.c_str(), passedTagList, tagsPos);
		}

		case tagClose : // if tagClose search left
		{
			_pEditView->execute(SCI_SETWORDCHARS, 0, (LPARAM)tagNameChars);
			int startPos = _pEditView->execute(SCI_WORDSTARTPOSITION, tagsPos.tagCloseStart+2, true);
			int endPos = _pEditView->execute(SCI_WORDENDPOSITION, tagsPos.tagCloseStart+2, true);
			
			_pEditView->execute(SCI_SETCHARSDEFAULT);
			char * tagName = new char[endPos-startPos+1];
			_pEditView->getText(tagName, startPos, endPos);

			basic_string<char> openTag = "<";
			openTag += tagName;
			openTag += "[ 	>]";

			basic_string<char> closeTag = "</";
			closeTag += tagName;
			closeTag += "[ 	]*>";
			
			delete [] tagName;

			vector<int> passedTagList;
			bool isFound = getMatchedTagPos(tagsPos.tagCloseStart, 0, openTag.c_str(), closeTag.c_str(), passedTagList, tagsPos);
			if (isFound)
				tagsPos.tagNameEnd = tagsPos.tagOpenStart + 1 + (endPos - startPos);

			return isFound;
		}

		case inSingleTag : // if in single tag
		{
			_pEditView->execute(SCI_SETWORDCHARS, 0, (LPARAM)tagNameChars);
			int endPos = _pEditView->execute(SCI_WORDENDPOSITION, tagsPos.tagOpenStart+1, true);
			tagsPos.tagNameEnd = endPos;
			_pEditView->execute(SCI_SETCHARSDEFAULT);

			tagsPos.tagCloseStart = -1;
			tagsPos.tagCloseEnd = -1;
			return true;
		}
		default: // if outOfTag, just quit
			return false;
		
	}
	//return false;
}

vector< pair<int, int> > XmlMatchedTagsHighlighter::getAttributesPos(int start, int end)
{
	vector< pair<int, int> > attributes;

	int bufLen = end - start + 1;
	char *buf = new char[bufLen+1];
	_pEditView->getText(buf, start, end);

	enum {\
		attr_invalid,\
		attr_key,\
		attr_pre_assign,\
		attr_assign,\
		attr_string,\
		attr_value,\
		attr_valid\
	} state = attr_invalid;

	int startPos = -1;
	int oneMoreChar = 1;
	int i = 0;
	for (; i < bufLen ; i++)
	{
		switch (buf[i])
		{
			case ' ':
			case '\t':
			case '\n':
			case '\r':
			{
				if (state == attr_key)
					state = attr_pre_assign;
				else if (state == attr_value)
				{
					state = attr_valid;
					oneMoreChar = 0;
				}
			}
			break;

			case '=':
			{
				if (state == attr_key || state == attr_pre_assign)
					state = attr_assign;
				else if (state == attr_assign || state == attr_value)
					state = attr_invalid;
			}
			break;

			case '"':
			{
				if (state == attr_string)
				{
					state = attr_valid;
					oneMoreChar = 1;
				}
				else if (state == attr_key || state == attr_pre_assign || state == attr_value)
					state = attr_invalid;
				else if (state == attr_assign)
					state = attr_string;
			}
			break;

			default:
			{
				if (state == attr_invalid)
				{
					state = attr_key;
					startPos = i;
				}
				else if (state == attr_pre_assign)
					state = attr_invalid;
				else if (state == attr_assign)
					state = attr_value;
			}
		}

		if (state == attr_valid)
		{
			attributes.push_back(pair<int, int>(start+startPos, start+i+oneMoreChar));
			state = attr_invalid;
		}
	}
	if (state == attr_value)
		attributes.push_back(pair<int, int>(start+startPos, start+i-1));

	delete [] buf;
	return attributes;
}

*/

bool XmlMatchedTagsHighlighter::getXmlMatchedTagsPos(XmlMatchedTagsPos &xmlTags)
{
	bool tagFound = false;
	int caret = _pEditView->execute(SCI_GETCURRENTPOS);
	FindResult openFound = findText(_T("<"), caret, 0, 0);
	
	if (openFound.success)
	{
		// Found the "<" before the caret, now check there isn't a > between that position and the caret.
		FindResult closeFound = findText(_T(">"), openFound.start, caret, 0, 0);

		if (!closeFound.success)
		{
			// We're in a tag (either a start tag or an end tag)
			int nextChar = _pEditView->execute(SCI_GETCHARAT, openFound.start + 1);


			/////////////////////////////////////////////////////////////////////////
			// CLOSE TAG   
			/////////////////////////////////////////////////////////////////////////
			if ('/' == nextChar)
			{
				xmlTags.tagCloseStart = openFound.start;
				int docLength = _pEditView->execute(SCI_GETLENGTH);
				FindResult endCloseTag = findText(_T(">"), caret, docLength, 0);
				if (endCloseTag.success)
				{
					xmlTags.tagCloseEnd = endCloseTag.end;
				}
				// Now find the tagName
				int position = caret + 1;

				// UTF-8 or ASCII tag name
				std::string tagName;
				
				// Checking for " is actually wrong here, but it means it works better with invalid XML
				while(nextChar != ' ' && nextChar != '/' && nextChar != '>' && nextChar != '\"' && position < docLength)
				{
					tagName.push_back((char)nextChar);
					++position;
					nextChar = _pEditView->execute(SCI_GETCHARAT, position);	
				}
				
				// Now we know where the end of the tag is, and we know what the tag is called
				xmlTags.tagNameEnd = position;

				/* Now we need to find the open tag.  The logic here is that we search for "<TAGNAME",
				 * then check the next character - if it's one of '>', ' ', '\"' then we know we've found 
				 * a relevant tag. 
				 * We then need to check if either
				 *    a) this tag is a self-closed tag - e.g. <TAGNAME attrib="value" />
				 * or b) this tag has another closing tag after it and before our closing tag
				 *       e.g.  <TAGNAME attrib="value">some text</TAGNAME></TAGNA|ME>
				 *             (cursor represented by |)
				 * If it's either of the above, then we continue searching, but only up to the
				 * the point of the last find. (So in the (b) example above, we'd only search backwards 
				 * from the first "<TAGNAME...", as we know there's a close tag for the opened tag.

				 * NOTE::  NEED TO CHECK THE ROTTEN CASE: ***********************************************************
				 * <TAGNAME attrib="value"><TAGNAME>something</TAGNAME></TAGNAME></TAGNA|ME>
				 * Maybe count all closing tags between start point and start of our end tag.???
				 */

			}
			else
			{
			/////////////////////////////////////////////////////////////////////////
			// OPEN TAG   
			/////////////////////////////////////////////////////////////////////////

			}
		}
	}
	return tagFound;
}


void XmlMatchedTagsHighlighter::tagMatch(bool doHiliteAttr) 
{
	// Clean up all marks of previous action
	_pEditView->clearIndicator(SCE_UNIVERSAL_TAGMATCH);
	_pEditView->clearIndicator(SCE_UNIVERSAL_TAGATTR);

	// Detect the current lang type. It works only with html and xml
	LangType lang = (_pEditView->getCurrentBuffer())->getLangType();

	if (lang != L_XML && lang != L_HTML && lang != L_PHP && lang != L_ASP && lang != L_JSP)
		return;

	// Get the original targets and search options to restore after tag matching operation
	int originalStartPos = _pEditView->execute(SCI_GETTARGETSTART);
	int originalEndPos = _pEditView->execute(SCI_GETTARGETEND);
	int originalSearchFlags = _pEditView->execute(SCI_GETSEARCHFLAGS);

	XmlMatchedTagsPos xmlTags;

    // Detect if it's a xml/html tag. If yes, Colour it!
	if (getXmlMatchedTagsPos(xmlTags))
	{
		_pEditView->execute(SCI_SETINDICATORCURRENT, SCE_UNIVERSAL_TAGMATCH);
		int openTagTailLen = 2;

		// Colourising the close tag firstly
		if ((xmlTags.tagCloseStart != -1) && (xmlTags.tagCloseEnd != -1))
		{
			_pEditView->execute(SCI_INDICATORFILLRANGE,  xmlTags.tagCloseStart, xmlTags.tagCloseEnd - xmlTags.tagCloseStart);
			// tag close is present, so it's not single tag
			openTagTailLen = 1;
		}

		// Colourising the open tag
		_pEditView->execute(SCI_INDICATORFILLRANGE,  xmlTags.tagOpenStart, xmlTags.tagNameEnd - xmlTags.tagOpenStart);
		_pEditView->execute(SCI_INDICATORFILLRANGE,  xmlTags.tagOpenEnd - openTagTailLen, openTagTailLen);

        
        // Colouising its attributs
        if (doHiliteAttr)
		{
			vector< pair<int, int> > attributes = getAttributesPos(xmlTags.tagNameEnd, xmlTags.tagOpenEnd - openTagTailLen);
			_pEditView->execute(SCI_SETINDICATORCURRENT,  SCE_UNIVERSAL_TAGATTR);
			for (size_t i = 0 ; i < attributes.size() ; i++)
			{
				_pEditView->execute(SCI_INDICATORFILLRANGE,  attributes[i].first, attributes[i].second - attributes[i].first);
			}
        }

        // Colouising indent guide line position
		if (_pEditView->isShownIndentGuide())
		{
			int columnAtCaret  = int(_pEditView->execute(SCI_GETCOLUMN, xmlTags.tagOpenStart));
			int columnOpposite = int(_pEditView->execute(SCI_GETCOLUMN, xmlTags.tagCloseStart));

			int lineAtCaret  = int(_pEditView->execute(SCI_LINEFROMPOSITION, xmlTags.tagOpenStart));
			int lineOpposite = int(_pEditView->execute(SCI_LINEFROMPOSITION, xmlTags.tagCloseStart));

			if (xmlTags.tagCloseStart != -1 && lineAtCaret != lineOpposite)
			{
				_pEditView->execute(SCI_BRACEHIGHLIGHT, xmlTags.tagOpenStart, xmlTags.tagCloseEnd-1);
				_pEditView->execute(SCI_SETHIGHLIGHTGUIDE, (columnAtCaret < columnOpposite)?columnAtCaret:columnOpposite);
			}
		}
	}

	// restore the original targets and search options to avoid the conflit with search/replace function
	_pEditView->execute(SCI_SETTARGETSTART, originalStartPos);
	_pEditView->execute(SCI_SETTARGETEND, originalEndPos);
	_pEditView->execute(SCI_SETSEARCHFLAGS, originalSearchFlags);
}
