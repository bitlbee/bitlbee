/* --------------------------------------------------------------------------
 *
 * License
 *
 * The contents of this file are subject to the Jabber Open Source License
 * Version 1.0 (the "JOSL").  You may not copy or use this file, in either
 * source code or executable form, except in compliance with the JOSL. You
 * may obtain a copy of the JOSL at http://www.jabber.org/ or at
 * http://www.opensource.org/.  
 *
 * Software distributed under the JOSL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the JOSL
 * for the specific language governing rights and limitations under the
 * JOSL.
 *
 * Copyrights
 * 
 * Portions created by or assigned to Jabber.com, Inc. are 
 * Copyright (c) 1999-2002 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 * 
 * Acknowledgements
 * 
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 * 
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 or later (the "GPL"), in which case
 * the provisions of the GPL are applicable instead of those above.  If you
 * wish to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the JOSL,
 * indicate your decision by deleting the provisions above and replace them
 * with the notice and other provisions required by the GPL.  If you do not
 * delete the provisions above, a recipient may use your version of this file
 * under either the JOSL or the GPL. 
 * 
 * 
 * --------------------------------------------------------------------------*/

#include "jabber.h"
#include <glib.h>

static xmlnode xmlnode_get_firstattrib(xmlnode parent);
static int xmlnode_get_type(xmlnode node);
static void xmlnode_insert_node(xmlnode parent, xmlnode node);

/* Internal routines */
static xmlnode _xmlnode_new(pool p, const char* name, unsigned int type)
{
    xmlnode result = NULL;
    if (type > NTYPE_LAST)
        return NULL;

    if (type != NTYPE_CDATA && name == NULL)
        return NULL;

    if (p == NULL)
    {
        p = pool_heap(1*1024);
    }

    /* Allocate & zero memory */
    result = (xmlnode)pmalloco(p, sizeof(_xmlnode));

    /* Initialize fields */
    if (type != NTYPE_CDATA)
        result->name = pstrdup(p,name);
    result->type = type;
    result->p = p;
    return result;
}

static xmlnode _xmlnode_append_sibling(xmlnode lastsibling, const char* name, unsigned int type)
{
    xmlnode result;

    result = _xmlnode_new(xmlnode_pool(lastsibling), name, type);
    if (result != NULL)
    {
        /* Setup sibling pointers */
        result->prev = lastsibling;
        lastsibling->next = result;
    }
    return result;
}

static xmlnode _xmlnode_insert(xmlnode parent, const char* name, unsigned int type)
{
    xmlnode result;

    if(parent == NULL || (type != NTYPE_CDATA && name == NULL)) return NULL;

    /* If parent->firstchild is NULL, simply create a new node for the first child */
    if (parent->firstchild == NULL)
    {
        result = _xmlnode_new(parent->p, name, type);
        parent->firstchild = result;
    }
    /* Otherwise, append this to the lastchild */
    else
    {
        result= _xmlnode_append_sibling(parent->lastchild, name, type);
    }
    result->parent = parent;
    parent->lastchild = result;
    return result;

}

static xmlnode _xmlnode_search(xmlnode firstsibling, const char* name, unsigned int type)
{
    xmlnode current;

    /* Walk the sibling list, looking for a NTYPE_TAG xmlnode with
    the specified name */
    current = firstsibling;
    while (current != NULL)
    {
        if ((current->type == type) && (j_strcmp(current->name, name) == 0))
            return current;
        else
            current = current->next;
    }
    return NULL;
}

static void _xmlnode_merge(xmlnode data)
{
    xmlnode cur;
    char *merge, *scur;
    int imerge;

    /* get total size of all merged cdata */
    imerge = 0;
    for(cur = data; cur != NULL && cur->type == NTYPE_CDATA; cur = cur->next)
        imerge += cur->data_sz;

    /* copy in current data and then spin through all of them and merge */
    scur = merge = pmalloc(data->p,imerge + 1);
    for(cur = data; cur != NULL && cur->type == NTYPE_CDATA; cur = cur->next)
    {
        memcpy(scur,cur->data,cur->data_sz);
        scur += cur->data_sz;
    }
    *scur = '\0';

    /* this effectively hides all of the merged-in chunks */
    data->next = cur;
    if(cur == NULL)
        data->parent->lastchild = data;
    else
        cur->prev = data;

    /* reset data */
    data->data = merge;
    data->data_sz = imerge;
    
}

static void _xmlnode_hide_sibling(xmlnode child)
{
    if(child == NULL)
        return;

    if(child->prev != NULL)
        child->prev->next = child->next;
    if(child->next != NULL)
        child->next->prev = child->prev;
}

static void _xmlnode_tag2str(spool s, xmlnode node, int flag)
{
    xmlnode tmp;

    if(flag==0 || flag==1)
    {
	    spooler(s,"<",xmlnode_get_name(node),s);
	    tmp = xmlnode_get_firstattrib(node);
	    while(tmp) {
	        spooler(s," ",xmlnode_get_name(tmp),"='",strescape(xmlnode_pool(node),xmlnode_get_data(tmp)),"'",s);
	        tmp = xmlnode_get_nextsibling(tmp);
	    }
	    if(flag==0)
	        spool_add(s,"/>");
	    else
	        spool_add(s,">");
    }
    else
    {
	    spooler(s,"</",xmlnode_get_name(node),">",s);
    }
}

static spool _xmlnode2spool(xmlnode node)
{
    spool s;
    int level=0,dir=0;
    xmlnode tmp;

    if(!node || xmlnode_get_type(node)!=NTYPE_TAG)
        return NULL;

    s = spool_new(xmlnode_pool(node));
    if(!s) return(NULL);

    while(1)
    {
        if(dir==0)
        {
    	    if(xmlnode_get_type(node) == NTYPE_TAG)
            {
                if(xmlnode_has_children(node))
                {
                    _xmlnode_tag2str(s,node,1);
                    node = xmlnode_get_firstchild(node);
                    level++;
                    continue;
                }else{
                    _xmlnode_tag2str(s,node,0);
                }
            }else{
                spool_add(s,strescape(xmlnode_pool(node),xmlnode_get_data(node)));
            }
        }

    	tmp = xmlnode_get_nextsibling(node);
        if(!tmp)
        {
            node = xmlnode_get_parent(node);
            level--;
            if(level>=0) _xmlnode_tag2str(s,node,2);
            if(level<1) break;
            dir = 1;
        }else{
            node = tmp;
            dir = 0;
        }
    }

    return s;
}


/* External routines */


/*
 *  xmlnode_new_tag -- create a tag node
 *  Automatically creates a memory pool for the node.
 *
 *  parameters
 *      name -- name of the tag
 *
 *  returns
 *      a pointer to the tag node
 *      or NULL if it was unsuccessfull
 */
xmlnode xmlnode_new_tag(const char* name)
{
    return _xmlnode_new(NULL, name, NTYPE_TAG);
}


/*
 *  xmlnode_insert_tag -- append a child tag to a tag
 *
 *  parameters
 *      parent -- pointer to the parent tag
 *      name -- name of the child tag
 *
 *  returns
 *      a pointer to the child tag node
 *      or NULL if it was unsuccessfull
 */
xmlnode xmlnode_insert_tag(xmlnode parent, const char* name)
{
    return _xmlnode_insert(parent, name, NTYPE_TAG);
}


/*
 *  xmlnode_insert_cdata -- append character data to a tag
 *
 *  parameters
 *      parent -- parent tag
 *      CDATA -- character data
 *      size -- size of CDATA
 *              or -1 for null-terminated CDATA strings
 *
 *  returns
 *      a pointer to the child CDATA node
 *      or NULL if it was unsuccessfull
 */
xmlnode xmlnode_insert_cdata(xmlnode parent, const char* CDATA, unsigned int size)
{
    xmlnode result;

    if(CDATA == NULL || parent == NULL)
        return NULL;

    if(size == -1)
        size = strlen(CDATA);

    result = _xmlnode_insert(parent, NULL, NTYPE_CDATA);
    if (result != NULL)
    {
        result->data = (char*)pmalloc(result->p, size + 1);
        memcpy(result->data, CDATA, size);
        result->data[size] = '\0';
        result->data_sz = size;
    }

    return result;
}


/*
 *  xmlnode_get_tag -- find given tag in an xmlnode tree
 *
 *  parameters
 *      parent -- pointer to the parent tag
 *      name -- "name" for the child tag of that name
 *              "name/name" for a sub child (recurses)
 *              "?attrib" to match the first tag with that attrib defined
 *              "?attrib=value" to match the first tag with that attrib and value
 *              "=cdata" to match the cdata contents of the child
 *              or any combination: "name/name/?attrib", "name=cdata", etc
 *
 *  results
 *      a pointer to the tag matching search criteria
 *      or NULL if search was unsuccessfull
 */
xmlnode xmlnode_get_tag(xmlnode parent, const char* name)
{
    char *str, *slash, *qmark, *equals;
    xmlnode step, ret;


    if(parent == NULL || parent->firstchild == NULL || name == NULL || name == '\0') return NULL;

    if(strstr(name, "/") == NULL && strstr(name,"?") == NULL && strstr(name, "=") == NULL)
        return _xmlnode_search(parent->firstchild, name, NTYPE_TAG);

    str = g_strdup(name);
    slash = strstr(str, "/");
    qmark = strstr(str, "?");
    equals = strstr(str, "=");

    if(equals != NULL && (slash == NULL || equals < slash) && (qmark == NULL || equals < qmark))
    { /* of type =cdata */

        *equals = '\0';
        equals++;

        for(step = parent->firstchild; step != NULL; step = xmlnode_get_nextsibling(step))
        {
            if(xmlnode_get_type(step) != NTYPE_TAG)
                continue;

            if(*str != '\0')
                if(j_strcmp(xmlnode_get_name(step),str) != 0)
                    continue;

            if(j_strcmp(xmlnode_get_data(step),equals) != 0)
                continue;

            break;
        }

        g_free(str);
        return step;
    }


    if(qmark != NULL && (slash == NULL || qmark < slash))
    { /* of type ?attrib */

        *qmark = '\0';
        qmark++;
        if(equals != NULL)
        {
            *equals = '\0';
            equals++;
        }

        for(step = parent->firstchild; step != NULL; step = xmlnode_get_nextsibling(step))
        {
            if(xmlnode_get_type(step) != NTYPE_TAG)
                continue;

            if(*str != '\0')
                if(j_strcmp(xmlnode_get_name(step),str) != 0)
                    continue;

            if(xmlnode_get_attrib(step,qmark) == NULL)
                continue;

            if(equals != NULL && j_strcmp(xmlnode_get_attrib(step,qmark),equals) != 0)
                continue;

            break;
        }

        g_free(str);
        return step;
    }


    *slash = '\0';
    ++slash;

    for(step = parent->firstchild; step != NULL; step = xmlnode_get_nextsibling(step))
    {
        if(xmlnode_get_type(step) != NTYPE_TAG) continue;

        if(j_strcmp(xmlnode_get_name(step),str) != 0)
            continue;

        ret = xmlnode_get_tag(step, slash);
        if(ret != NULL)
        {
            g_free(str);
            return ret;
        }
    }

    g_free(str);
    return NULL;
}


/* return the cdata from any tag */
char *xmlnode_get_tag_data(xmlnode parent, const char *name)
{
    xmlnode tag;

    tag = xmlnode_get_tag(parent, name);
    if(tag == NULL) return NULL;

    return xmlnode_get_data(tag);
}


void xmlnode_put_attrib(xmlnode owner, const char* name, const char* value)
{
    xmlnode attrib;

    if(owner == NULL || name == NULL || value == NULL) return;

    /* If there are no existing attributs, allocate a new one to start
    the list */
    if (owner->firstattrib == NULL)
    {
        attrib = _xmlnode_new(owner->p, name, NTYPE_ATTRIB);
        owner->firstattrib = attrib;
        owner->lastattrib  = attrib;
    }
    else
    {
        attrib = _xmlnode_search(owner->firstattrib, name, NTYPE_ATTRIB);
        if(attrib == NULL)
        {
            attrib = _xmlnode_append_sibling(owner->lastattrib, name, NTYPE_ATTRIB);
            owner->lastattrib = attrib;
        }
    }
    /* Update the value of the attribute */
    attrib->data_sz = strlen(value);
    attrib->data    = pstrdup(owner->p, value);

}

char* xmlnode_get_attrib(xmlnode owner, const char* name)
{
    xmlnode attrib;

    if (owner != NULL && owner->firstattrib != NULL)
    {
        attrib = _xmlnode_search(owner->firstattrib, name, NTYPE_ATTRIB);
        if (attrib != NULL)
            return (char*)attrib->data;
    }
    return NULL;
}

static xmlnode xmlnode_get_firstattrib(xmlnode parent)
{
    if (parent != NULL)
        return parent->firstattrib;
    return NULL;
}

xmlnode xmlnode_get_firstchild(xmlnode parent)
{
    if (parent != NULL)
        return parent->firstchild;
    return NULL;
}

xmlnode xmlnode_get_nextsibling(xmlnode sibling)
{
    if (sibling != NULL)
        return sibling->next;
    return NULL;
}

xmlnode xmlnode_get_parent(xmlnode node)
{
    if (node != NULL)
        return node->parent;
    return NULL;
}

char* xmlnode_get_name(xmlnode node)
{
    if (node != NULL)
        return node->name;
    return NULL;
}

char* xmlnode_get_data(xmlnode node)
{
    if(xmlnode_get_type(node) == NTYPE_TAG) /* loop till we find a CDATA in the children */
        for(node = xmlnode_get_firstchild(node); node != NULL; node = xmlnode_get_nextsibling(node))
            if(xmlnode_get_type(node) == NTYPE_CDATA) break;

    if(node == NULL) return NULL;

    /* check for a dirty node w/ unassembled cdata chunks */
    if(xmlnode_get_type(node->next) == NTYPE_CDATA)
        _xmlnode_merge(node);

    return node->data;
}

static int xmlnode_get_datasz(xmlnode node)
{
    if(xmlnode_get_type(node) != NTYPE_CDATA) return 0;

    /* check for a dirty node w/ unassembled cdata chunks */
    if(xmlnode_get_type(node->next) == NTYPE_CDATA)
        _xmlnode_merge(node);
    return node->data_sz;
}

static int xmlnode_get_type(xmlnode node)
{
    if (node != NULL)
        return node->type;
    return NTYPE_UNDEF;
}

int xmlnode_has_children(xmlnode node)
{
    if ((node != NULL) && (node->firstchild != NULL))
        return 1;
    return 0;
}

static int xmlnode_has_attribs(xmlnode node)
{
    if ((node != NULL) && (node->firstattrib != NULL))
        return 1;
    return 0;
}

pool xmlnode_pool(xmlnode node)
{
    if (node != NULL)
        return node->p;
    return (pool)NULL;
}

void xmlnode_hide_attrib(xmlnode parent, const char *name)
{
    xmlnode attrib;

    if(parent == NULL || parent->firstattrib == NULL || name == NULL)
        return;

    attrib = _xmlnode_search(parent->firstattrib, name, NTYPE_ATTRIB);
    if(attrib == NULL)
        return;

    /* first fix up at the child level */
    _xmlnode_hide_sibling(attrib);

    /* next fix up at the parent level */
    if(parent->firstattrib == attrib)
        parent->firstattrib = attrib->next;
    if(parent->lastattrib == attrib)
        parent->lastattrib = attrib->prev;
}



/*
 *  xmlnode2str -- convert given xmlnode tree into a string
 *
 *  parameters
 *      node -- pointer to the xmlnode structure
 *
 *  results
 *      a pointer to the created string
 *      or NULL if it was unsuccessfull
 */
char *xmlnode2str(xmlnode node)
{
     return spool_print(_xmlnode2spool(node));
}

/* loop through both a and b comparing everything, attribs, cdata, children, etc */
static int xmlnode_cmp(xmlnode a, xmlnode b)
{
    int ret = 0;

    while(1)
    {
        if(a == NULL && b == NULL)
            return 0;

        if(a == NULL || b == NULL)
            return -1;

        if(xmlnode_get_type(a) != xmlnode_get_type(b))
            return -1;

        switch(xmlnode_get_type(a))
        {
        case NTYPE_ATTRIB:
            ret = j_strcmp(xmlnode_get_name(a), xmlnode_get_name(b));
            if(ret != 0)
                return -1;
            ret = j_strcmp(xmlnode_get_data(a), xmlnode_get_data(b));
            if(ret != 0)
                return -1;
            break;
        case NTYPE_TAG:
            ret = j_strcmp(xmlnode_get_name(a), xmlnode_get_name(b));
            if(ret != 0)
                return -1;
            ret = xmlnode_cmp(xmlnode_get_firstattrib(a), xmlnode_get_firstattrib(b));
            if(ret != 0)
                return -1;
            ret = xmlnode_cmp(xmlnode_get_firstchild(a), xmlnode_get_firstchild(b));
            if(ret != 0)
                return -1;
            break;
        case NTYPE_CDATA:
            ret = j_strcmp(xmlnode_get_data(a), xmlnode_get_data(b));
            if(ret != 0)
                return -1;
        }
        a = xmlnode_get_nextsibling(a);
        b = xmlnode_get_nextsibling(b);
    }
}


xmlnode xmlnode_insert_tag_node(xmlnode parent, xmlnode node)
{
    xmlnode child;

    child = xmlnode_insert_tag(parent, xmlnode_get_name(node));
    if (xmlnode_has_attribs(node))
        xmlnode_insert_node(child, xmlnode_get_firstattrib(node));
    if (xmlnode_has_children(node))
        xmlnode_insert_node(child, xmlnode_get_firstchild(node));

    return child;
}

/* places copy of node and node's siblings in parent */
static void xmlnode_insert_node(xmlnode parent, xmlnode node)
{
    if(node == NULL || parent == NULL)
        return;

    while(node != NULL)
    {
        switch(xmlnode_get_type(node))
        {
        case NTYPE_ATTRIB:
            xmlnode_put_attrib(parent, xmlnode_get_name(node), xmlnode_get_data(node));
            break;
        case NTYPE_TAG:
            xmlnode_insert_tag_node(parent, node);
            break;
        case NTYPE_CDATA:
            xmlnode_insert_cdata(parent, xmlnode_get_data(node), xmlnode_get_datasz(node));
        }
        node = xmlnode_get_nextsibling(node);
    }
}


void xmlnode_free(xmlnode node)
{
    if(node == NULL)
        return;

    pool_free(node->p);
}
