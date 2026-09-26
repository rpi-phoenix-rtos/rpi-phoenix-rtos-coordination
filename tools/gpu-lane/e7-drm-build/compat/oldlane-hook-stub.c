/* E7: the Mesa fork's old-lane __phoenix__ hook in v3d_resource.c:863/935 calls into the
 * in-process winsys. The new lane must not compile that hook; stubbed here only to finish the link probe. */
int v3d_phoenix_peek_next_scanout(void);
int v3d_phoenix_peek_next_scanout(void) { return 0; }
